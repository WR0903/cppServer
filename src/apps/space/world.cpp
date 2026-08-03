#include "world.h"
#include "player_manager_component.h"
#include "player_component_detail.h"
#include "player_component_bag.h"
#include "aoi_component.h"

#include "libserver/message/message_system_help.h"
#include "libserver/message/message_system.h"
#include "libplayer/player_component_last_map.h"
#include "move_component.h"

#include <algorithm>
#include <vector>

void World::Awake(int worldId)
{
    //LOG_DEBUG("create world. id:" << worldId << " sn:" << _sn << " space app id:" << Global::GetAppIdFromSN(_sn));

    _worldId = worldId;

    AddComponent<PlayerManagerComponent>();
    AddComponent<AoiComponent>();

    AddTimer(0, 10, false, 0, BindFunP0(this, &World::SyncWorldToGather));
    AddTimer(0, 1, false, 0, BindFunP0(this, &World::SyncAppearTimer));
    AddTimer(0, 1, false, 0, BindFunP0(this, &World::FlushMovesBroadcast));

    // message
    auto pMsgSystem = GetSystemManager()->GetMessageSystem();
    pMsgSystem->RegisterFunction(this, Proto::MsgId::MI_NetworkDisconnect, BindFunP1(this, &World::HandleNetworkDisconnect));
    pMsgSystem->RegisterFunction(this, Proto::MsgId::G2S_SyncPlayer, BindFunP1(this, &World::HandleSyncPlayer));

    pMsgSystem->RegisterFunctionFilter<Player>(this, Proto::MsgId::G2S_RequestSyncPlayer, BindFunP1(this, &World::GetPlayer), BindFunP2(this, &World::HandleRequestSyncPlayer));
    pMsgSystem->RegisterFunctionFilter<Player>(this, Proto::MsgId::G2S_RemovePlayer, BindFunP1(this, &World::GetPlayer), BindFunP2(this, &World::HandleG2SRemovePlayer));

    pMsgSystem->RegisterFunctionFilter<Player>(this, Proto::MsgId::C2S_Move, BindFunP1(this, &World::GetPlayer), BindFunP2(this, &World::HandleMove));

    pMsgSystem->RegisterFunctionFilter<Player>(this, Proto::MsgId::C2S_BagSync, BindFunP1(this, &World::GetPlayer), BindFunP2(this, &World::HandleBagSync));
    pMsgSystem->RegisterFunctionFilter<Player>(this, Proto::MsgId::C2S_ItemUse, BindFunP1(this, &World::GetPlayer), BindFunP2(this, &World::HandleItemUse));

}

void World::BackToPool()
{
    _addPlayer.clear();
    _pendingMoves.clear();
    _lastFlushMovesTime = 0;
}

Player* World::GetPlayer(NetIdentify* pIdentify)
{
    auto pTags = pIdentify->GetTagKey();
    const auto pTagPlayer = pTags->GetTagValue(TagType::Player);
    if (pTagPlayer == nullptr)
        return nullptr;

    auto pPlayerMgr = GetComponent<PlayerManagerComponent>();
    return pPlayerMgr->GetPlayerBySn(pTagPlayer->KeyInt64);
}

void World::HandleNetworkDisconnect(Packet* pPacket)
{
    //LOG_DEBUG("world id:" << _worldId << " disconnect." << pPacket);

    auto pTags = pPacket->GetTagKey();
    const auto pTagPlayer = pTags->GetTagValue(TagType::Player);
    if (pTagPlayer != nullptr)
    {
        auto pPlayerMgr = GetComponent<PlayerManagerComponent>();
        const auto pPlayer = pPlayerMgr->GetPlayerBySn(pTagPlayer->KeyInt64);
        if (pPlayer == nullptr)
        {
            LOG_ERROR("world. net disconnect. can't find player. player sn:" << pTagPlayer->KeyInt64);
            return;
        }

        Proto::SavePlayer protoSave;
        protoSave.set_player_sn(pPlayer->GetPlayerSN());
        pPlayer->SerializeToProto(protoSave.mutable_player());
        MessageSystemHelp::SendPacket(Proto::MsgId::G2DB_SavePlayer, protoSave, APP_DB_MGR);

        // 玩家掉线，先从AOI中移除
        auto pAoi = GetComponent<AoiComponent>();
        pAoi->Leave(pTagPlayer->KeyInt64);

        pPlayerMgr->RemovePlayerBySn(pTagPlayer->KeyInt64);
    }
    else
    {
        // dbmgr, appmgr or game断线
        const auto pTagApp = pTags->GetTagValue(TagType::App);
        if (pTagApp != nullptr)
        {
            auto pPlayerMgr = GetComponent<PlayerManagerComponent>();
            pPlayerMgr->RemoveAllPlayers(pPacket);
        }
    }
}

void World::SyncWorldToGather()
{
    Proto::WorldSyncToGather proto;
    proto.set_world_sn(GetSN());
    proto.set_world_id(GetWorldId());

    const int online = GetComponent<PlayerManagerComponent>()->OnlineSize();
    proto.set_online(online);

    MessageSystemHelp::DispatchPacket(Proto::MsgId::MI_WorldSyncToGather, proto, nullptr);
}

inline void CreateProtoRoleAppear(Player* pPlayer, Proto::RoleAppear& protoAppear)
{
    auto proto = protoAppear.add_role();
    proto->set_name(pPlayer->GetName().c_str());
    proto->set_sn(pPlayer->GetPlayerSN());

    const auto pBaseInfo = pPlayer->GetComponent<PlayerComponentDetail>();
    proto->set_gender(pBaseInfo->GetGender());

    const auto pComponentLastMap = pPlayer->GetComponent<PlayerComponentLastMap>();
    const auto pLastMap = pComponentLastMap->GetCur();
    pLastMap->Position.SerializeToProto(proto->mutable_position());
}

void World::SyncAppearTimer()
{
    auto pPlayerMgr = GetComponent<PlayerManagerComponent>();

    if (!_addPlayer.empty())
    {
        // 1.新增的数据，同步到全地图
        Proto::RoleAppear protoNewAppear;
        for (auto id : _addPlayer)
        {
            // 有可能瞬间已下线
            const auto pPlayer = pPlayerMgr->GetPlayerBySn(id);
            if (pPlayer == nullptr)
                continue;

            CreateProtoRoleAppear(pPlayer, protoNewAppear);
        }

        if (protoNewAppear.role_size() > 0)
            BroadcastPacket(Proto::MsgId::S2C_RoleAppear, protoNewAppear);

        // 2.原始玩家的数据，同步给新增的玩家
        Proto::RoleAppear protoOther;
        const auto players = pPlayerMgr->GetAll();
        for (const auto one : *players)
        {
            // 排除新玩家
            if (_addPlayer.find(one.first) != _addPlayer.end())
                continue;

            const auto role = one.second;
            CreateProtoRoleAppear(role, protoOther);
        }

        if (protoOther.role_size() > 0)
            BroadcastPacket(Proto::MsgId::S2C_RoleAppear, protoOther, _addPlayer);

        _addPlayer.clear();
    }
}

void World::HandleSyncPlayer(Packet* pPacket)
{
    auto proto = pPacket->ParseToProto<Proto::SyncPlayer>();
    const auto playerSn = proto.player().sn();
    //const int gameAppId = proto.app_id();

    auto pPlayerMgr = GetComponent<PlayerManagerComponent>();
    auto pPlayer = pPlayerMgr->AddPlayer(playerSn, GetSN(), pPacket);
    if (pPlayer == nullptr)
    {
        LOG_ERROR("failed to add player. player sn:" << playerSn);
        return;
    }

    pPlayer->ParserFromProto(playerSn, proto.player());
    pPlayer->AddComponent<PlayerComponentDetail>();
    pPlayer->AddComponent<PlayerComponentBag>();

    const auto pComponentLastMap = pPlayer->AddComponent<PlayerComponentLastMap>();
    pComponentLastMap->EnterWorld(_worldId, _sn);
    const auto pLastMap = pComponentLastMap->GetCur();

    //LOG_DEBUG("world. recv teleport. map id:" << _worldId << " world sn:" << GetSN() << " playerSn:" << pPlayer->GetPlayerSN());

    //通知客户端进入地图
    Proto::EnterWorld protoEnterWorld;
    protoEnterWorld.set_world_id(_worldId);
    pLastMap->Position.SerializeToProto(protoEnterWorld.mutable_position());
    MessageSystemHelp::SendPacket(Proto::MsgId::S2C_EnterWorld, protoEnterWorld, pPlayer);

    _addPlayer.insert(playerSn);

    // 将玩家加入AOI
    auto pAoi = GetComponent<AoiComponent>();
    pAoi->Enter(playerSn, pLastMap->Position);

    // 启动玩家独立定时存盘（10-60 秒随机间隔）
    pPlayer->StartSaveTimer();
}

void World::BroadcastPacket(Proto::MsgId msgId, google::protobuf::Message& proto)
{
    auto pPlayerMgr = GetComponent<PlayerManagerComponent>();
    const auto players = pPlayerMgr->GetAll();
    for (const auto pair : *players)
    {
        //LOG_DEBUG("broadcast msgId:" << Log4Help::GetMsgIdName(msgId).c_str() << " player sn:" << pair.second->GetPlayerSN());
        MessageSystemHelp::SendPacket(msgId, proto, pair.second);
    }
}

void World::BroadcastPacket(Proto::MsgId msgId, google::protobuf::Message& proto, std::set<uint64> players)
{
    auto pPlayerMgr = GetComponent<PlayerManagerComponent>();
    for (const auto one : players)
    {
        const auto pPlayer = pPlayerMgr->GetPlayerBySn(one);
        if (pPlayer == nullptr)
            continue;

        //LOG_DEBUG("broadcast msgId:" << Log4Help::GetMsgIdName(msgId).c_str() << " player sn:" << one);
        MessageSystemHelp::SendPacket(msgId, proto, pPlayer);
    }
}

void World::HandleRequestSyncPlayer(Player* pPlayer, Packet* pPacket)
{
    Proto::SyncPlayer protoSync;
    protoSync.set_account(pPlayer->GetAccount().c_str());
    pPlayer->SerializeToProto(protoSync.mutable_player());

    MessageSystemHelp::SendPacket(Proto::MsgId::S2G_SyncPlayer, protoSync, pPlayer);
}

void World::HandleG2SRemovePlayer(Player* pPlayer, Packet* pPacket)
{
    auto proto = pPacket->ParseToProto<Proto::RemovePlayer>();
    if (proto.player_sn() != pPlayer->GetPlayerSN())
    {
        LOG_ERROR("HandleTeleportAfter. proto.player_sn() != pPlayer->GetPlayerSN()");
        return;
    }

    // 跳转地图前先存盘
    Proto::SavePlayer protoSave;
    protoSave.set_player_sn(pPlayer->GetPlayerSN());
    pPlayer->SerializeToProto(protoSave.mutable_player());
    MessageSystemHelp::SendPacket(Proto::MsgId::G2DB_SavePlayer, protoSave, APP_DB_MGR);

    // 从AOI中移除
    auto pAoi = GetComponent<AoiComponent>();
    pAoi->Leave(pPlayer->GetPlayerSN());

    auto pPlayerMgr = GetComponent<PlayerManagerComponent>();
    pPlayerMgr->RemovePlayerBySn(pPlayer->GetPlayerSN());

    // 通知其他玩家
    Proto::RoleDisAppear disAppear;
    disAppear.set_sn(pPlayer->GetPlayerSN());
    BroadcastPacket(Proto::MsgId::S2C_RoleDisAppear, disAppear);
}

void World::HandleMove(Player* pPlayer, Packet* pPacket)
{
    auto proto = pPacket->ParseToProto<Proto::Move>();
    proto.set_player_sn(pPlayer->GetPlayerSN());
    const auto positions = proto.mutable_position();
    auto pMoveComponent = pPlayer->GetComponent<MoveComponent>();
    if (pMoveComponent == nullptr)
    {
        pMoveComponent = pPlayer->AddComponent<MoveComponent>();
    }

    std::queue<Vector3> pos;
    Vector3 lastPos(0, 0, 0);
    for (auto index = 0; index < proto.position_size(); index++)
    {
        Vector3 v3(0, 0, 0);
        v3.ParserFromProto(positions->Get(index));
        pos.push(v3);
        lastPos = v3;  // 记录最后一个路径点（目标位置）
    }

    const auto pComponentLastMap = pPlayer->GetComponent<PlayerComponentLastMap>();
    pMoveComponent->Update(pos, pComponentLastMap->GetCur()->Position);

    // 更新玩家在AOI中的位置
    auto pAoi = GetComponent<AoiComponent>();
    if (proto.position_size() > 0)
    {
        pAoi->Move(pPlayer->GetPlayerSN(), lastPos);
    }

    // 将移动数据缓存，等待批量广播（广播合并）
    // 同一个玩家多次移动只保留最新的一次
    PendingMove& pending = _pendingMoves[pPlayer->GetPlayerSN()];
    pending.proto = proto;
    pending.lastPos = lastPos;

    // 检查是否到了广播时间（MOVE_BROADCAST_INTERVAL_MS 间隔）
    const auto curTime = Global::GetInstance()->TimeTick;
    if (curTime - _lastFlushMovesTime >= MOVE_BROADCAST_INTERVAL_MS)
    {
        FlushMovesBroadcast();
    }
}

void World::HandleBagSync(Player* pPlayer, Packet* pPacket)
{
    auto pBag = pPlayer->GetComponent<PlayerComponentBag>();
    if (pBag == nullptr)
        return;

    Proto::BagSync proto;
    proto.set_capacity(pBag->GetCapacity());
    for (const auto& pair : pBag->GetItems())
    {
        auto item = proto.add_items();
        item->CopyFrom(pair.second);
    }
    MessageSystemHelp::SendPacket(Proto::MsgId::S2C_BagSync, proto, pPlayer);
}

void World::HandleItemUse(Player* pPlayer, Packet* pPacket)
{
    auto proto = pPacket->ParseToProto<Proto::ItemUse>();
    auto pBag = pPlayer->GetComponent<PlayerComponentBag>();
    if (pBag == nullptr)
        return;

    if (!pBag->RemoveItem(proto.item_id(), proto.count()))
    {
        LOG_WARN("item use failed. player sn:" << pPlayer->GetPlayerSN()
            << " item id:" << proto.item_id() << " count:" << proto.count());
        return;
    }

    // 使用物品效果（TODO: 根据物品类型执行不同逻辑）

    // 同步背包给客户端
    Proto::BagSync protoSync;
    protoSync.set_capacity(pBag->GetCapacity());
    for (const auto& pair : pBag->GetItems())
    {
        auto item = protoSync.add_items();
        item->CopyFrom(pair.second);
    }
    MessageSystemHelp::SendPacket(Proto::MsgId::S2C_BagSync, protoSync, pPlayer);
}

void World::FlushMovesBroadcast()
{
    if (_pendingMoves.empty())
        return;

    _lastFlushMovesTime = Global::GetInstance()->TimeTick;

    auto pAoi = GetComponent<AoiComponent>();
    auto pPlayerMgr = GetComponent<PlayerManagerComponent>();

    // 遍历所有缓存的移动数据，批量广播
    for (auto& pair : _pendingMoves)
    {
        const uint64 playerSn = pair.first;
        PendingMove& pending = pair.second;

        // 获取九宫格范围内的所有玩家
        std::set<uint64> nearbyPlayers = pAoi->GetNearbyPlayers(playerSn);

        // 如果附近玩家数量超过 MAX_BROADCAST_PLAYERS，只选最近的 MAX_BROADCAST_PLAYERS 人同步
        if (static_cast<int>(nearbyPlayers.size()) > MAX_BROADCAST_PLAYERS)
        {
            // 计算每个附近玩家与当前移动玩家的距离，按距离排序取最近的
            struct PlayerDist
            {
                uint64 sn;
                float dist;
            };

            std::vector<PlayerDist> distList;
            distList.reserve(nearbyPlayers.size());

            for (uint64 nearbySn : nearbyPlayers)
            {
                // 移动发起者自己必须收到确认，始终包含
                if (nearbySn == playerSn)
                    continue;

                const auto pNearbyPlayer = pPlayerMgr->GetPlayerBySn(nearbySn);
                if (pNearbyPlayer == nullptr)
                    continue;

                const auto pLastMap = pNearbyPlayer->GetComponent<PlayerComponentLastMap>();
                if (pLastMap == nullptr || pLastMap->GetCur() == nullptr)
                    continue;

                const Vector3& nearbyPos = pLastMap->GetCur()->Position;
                const float dist = pending.lastPos.GetDistance(nearbyPos);
                distList.push_back({ nearbySn, dist });
            }

            // 按距离升序排序
            std::sort(distList.begin(), distList.end(), [](const PlayerDist& a, const PlayerDist& b) {
                return a.dist < b.dist;
            });

            // 重建接收者集合：自己 + 最近的 (MAX_BROADCAST_PLAYERS - 1) 人
            nearbyPlayers.clear();
            nearbyPlayers.insert(playerSn);  // 发起者自己必须收到

            const int maxOthers = MAX_BROADCAST_PLAYERS - 1;
            for (int i = 0; i < maxOthers && i < static_cast<int>(distList.size()); i++)
            {
                nearbyPlayers.insert(distList[i].sn);
            }
        }

        // 广播移动消息
        BroadcastPacket(Proto::MsgId::S2C_Move, pending.proto, nearbyPlayers);
    }

    _pendingMoves.clear();
}

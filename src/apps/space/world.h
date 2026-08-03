#pragma once
#include "libserver/ecs/system.h"
#include "libplayer/world_base.h"
#include "libserver/ecs/entity.h"
#include "libserver/network/socket_object.h"
#include "aoi_component.h"

#include <unordered_map>

// 单次移动广播最大同步人数（九宫格内超过此数量时只同步最近的玩家）
constexpr int MAX_BROADCAST_PLAYERS = 100;

// 移动广播合并间隔（毫秒）
constexpr uint64 MOVE_BROADCAST_INTERVAL_MS = 200;

class Player;
class World :public Entity<World>, public IWorld, public IAwakeFromPoolSystem<int>
{
public:
    void Awake(int worldId) override;
    void BackToPool() override;

protected:
    Player* GetPlayer(NetIdentify* pIdentify);

    void BroadcastPacket(Proto::MsgId msgId, google::protobuf::Message& proto);
    void BroadcastPacket(Proto::MsgId msgId, google::protobuf::Message& proto, std::set<uint64> players);

    void HandleNetworkDisconnect(Packet* pPacket);
    void HandleSyncPlayer(Packet* pPacket);
    void HandleRequestSyncPlayer(Player* pPlayer, Packet* pPacket);
    void HandleG2SRemovePlayer(Player* pPlayer, Packet* pPacket);
    void HandleMove(Player* pPlayer, Packet* pPacket);
    void HandleBagSync(Player* pPlayer, Packet* pPacket);
    void HandleItemUse(Player* pPlayer, Packet* pPacket);

private:
    void SyncWorldToGather();
    void SyncAppearTimer();
    void FlushMovesBroadcast();

private:
    // 缓存1秒内增加或是删除的玩家
    std::set<uint64> _addPlayer;

    // ===== 移动广播合并 =====
    // 缓存每个玩家的最新移动协议（key: playerSn）
    // 在定时器触发时批量广播，避免每次移动都立即广播
    struct PendingMove
    {
        Proto::Move proto;
        Vector3 lastPos{0, 0, 0};  // 用于计算距离排序
    };
    std::unordered_map<uint64, PendingMove> _pendingMoves;

    // 上次刷新移动广播的时间
    uint64 _lastFlushMovesTime{ 0 };
};


#pragma once
#include "libserver/utils/common.h"
#include "libserver/network/socket_object.h"
#include "libserver/ecs/entity.h"
#include "libserver/ecs/system.h"

#include <random>

class Player :public Entity<Player>, public NetIdentify,
    virtual public IAwakeFromPoolSystem<NetIdentify*, std::string>,
    virtual public IAwakeFromPoolSystem<NetIdentify*, uint64, uint64>
{
public:
    void Awake(NetIdentify* pIdentify, std::string account) override;
    void Awake(NetIdentify* pIdentify, uint64 playerSn, uint64 worldSn) override;
    void BackToPool() override;

    std::string GetAccount() const;
    std::string GetName() const;
    uint64 GetPlayerSN() const;

    Proto::Player& GetPlayerProto();

    void ParseFromStream(uint64 playerSn, std::stringstream* pOpStream);
    void ParserFromProto(uint64 playerSn, const Proto::Player& proto);
    void SerializeToProto(Proto::Player* pProto) const;

    // 启动定时存盘（10-60 秒随机间隔，每个玩家独立计时）
    void StartSaveTimer();

protected:
    std::string _account{ "" };
    std::string _name{ "" };

    uint64 _playerSn{ 0 };
    Proto::Player _player;

private:
    void OnSaveTimer();

    int _saveIntervalMin{ 10 };
    int _saveIntervalMax{ 60 };
    std::uniform_int_distribution<int> _saveDist{ _saveIntervalMin, _saveIntervalMax };
    std::mt19937 _saveGen{ std::random_device{}() };
};


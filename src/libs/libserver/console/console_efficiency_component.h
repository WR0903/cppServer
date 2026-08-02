#pragma once
#include "ecs/system.h"
#include "ecs/entity.h"
#include "utils/util_time.h"

#include <map>

class Packet;

struct ThreadEfficiencyInfo
{
    ThreadType ThreadTypeKey;
    uint64 UpdateTime;  // udpate平均时间
    timeutil::Time LastRecvTime;
    uint64 UpdateTimeMax;
};

class ConsoleEfficiencyComponent :public Entity<ConsoleEfficiencyComponent>, public IAwakeSystem<>
{
public:
    void Awake() override;
    void BackToPool() override;

private:
    void HandleCmdEfficiency(Packet* pPacket);
    void HandleEfficiency(Packet* pPacket);

private:
    // threa id, ThreadEfficiencyInfo
    std::map<std::string, ThreadEfficiencyInfo> _threads;
};


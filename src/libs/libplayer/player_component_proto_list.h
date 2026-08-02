#pragma once

#include "libserver/utils/common.h"
#include "libserver/ecs/component.h"
#include "libserver/ecs/system.h"

#include <sstream>

// player 组件，处理PlayerList
class PlayerComponentProtoList :public Component<PlayerComponentProtoList>, public IAwakeFromPoolSystem<>
{
public:
    void Awake() override {}
    void BackToPool() override;

    void Parse(Proto::PlayerList& proto);
    std::stringstream* GetProto(uint64 sn);

private:
    std::map<uint64, std::stringstream*> _protos;
};


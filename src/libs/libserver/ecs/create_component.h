#pragma once

#include "ecs/entity.h"
#include "ecs/system.h"
#include "message/message_system.h"

class Packet;
class CreateComponentC :public Entity<CreateComponentC>, public IAwakeSystem<>
{
public:
    void Awake() override;
    void BackToPool() override;

private:
    void HandleCreateComponent(Packet* pPacket) const;
    void HandleRemoveComponent(Packet* pPacket);
    void HandleCreateSystem(Packet* pPacket);
};


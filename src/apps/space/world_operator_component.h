#pragma once
#include "libserver/ecs/entity.h"
#include "libserver/ecs/system.h"

class Packet;

class WorldOperatorComponent : public Entity<WorldOperatorComponent>, public IAwakeSystem<>
{
public:
	void Awake() override;
    void BackToPool() override;

private:
	void HandleCreateWorld(Packet* pPacket);
};

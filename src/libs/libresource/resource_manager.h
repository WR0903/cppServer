#pragma once

#include "libserver/ecs/entity.h"
#include "resource_world.h"

class ResourceManager :public Entity<ResourceManager>, public IAwakeSystem<>
{
public:
    void Awake() override;
    void BackToPool() override;

public:
    ResourceWorldMgr* Worlds;
};

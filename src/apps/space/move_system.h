#pragma once
#include "libserver/ecs/system.h"
#include "libserver/utils/util_time.h"
#include "libserver/ecs/component_collections.h"

class MoveSystem : public ISystem<MoveSystem>
{
public:
    MoveSystem();
    void Update(EntitySystem* pEntities) override;

private:
    timeutil::Time _lastTime;
    ComponentCollections* _pCollections{ nullptr };
};


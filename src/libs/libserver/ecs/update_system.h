#pragma once

#include "ecs/system.h"
#include "ecs/component_collections.h"

class UpdateSystem : virtual public ISystem<UpdateSystem>
{
public:
    void Update(EntitySystem* pEntities) override;

private:
    ComponentCollections* _pCollections{ nullptr };
};

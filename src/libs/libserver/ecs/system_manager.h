#pragma once

#include "ecs/disposable.h"
#include "ecs/system.h"
#include "utils/common.h"
#include "thread/thread_type.h"

#include <list>
#include <random>
#include "ecs/check_time_component.h"
#include "ecs/update_system.h"

class EntitySystem;
class MessageSystem;
class DynamicObjectPoolCollector;

class SystemManager : virtual public IDisposable, public CheckTimeComponent
{
public:
    SystemManager();
    void InitComponent(ThreadType threadType);

    virtual void Update();
    void Dispose() override;

    MessageSystem* GetMessageSystem() const;
    EntitySystem* GetEntitySystem() const;
    UpdateSystem* GetUpdateSystem() const;

    DynamicObjectPoolCollector* GetPoolCollector() const;

    std::default_random_engine* GetRandomEngine() const;

    void AddSystem(const std::string& name);

protected:
    MessageSystem* _pMessageSystem;
    EntitySystem* _pEntitySystem;
    UpdateSystem* _pUpdateSystem;

    std::list<System*> _systems;

    std::default_random_engine* _pRandomEngine;

    DynamicObjectPoolCollector* _pPoolCollector;
};

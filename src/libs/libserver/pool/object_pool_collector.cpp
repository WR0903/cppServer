#include "pool/object_pool_collector.h"
#include "pool/object_pool.h"
#include "ecs/system_manager.h"

DynamicObjectPoolCollector::DynamicObjectPoolCollector(SystemManager* pSys)
{
    _pSystemManager = pSys;
}

void DynamicObjectPoolCollector::Dispose()
{
    for (auto iter = _pools.begin(); iter != _pools.end(); ++iter)
    {
        auto pObj = iter->second;
        pObj->Dispose();
        delete pObj;
    }

    _pools.clear();
}

void DynamicObjectPoolCollector::Update()
{
    for (auto iter = _pools.begin(); iter != _pools.end(); ++iter)
    {
        iter->second->Update();
    }
}

void DynamicObjectPoolCollector::Show()
{
    for (auto iter = _pools.begin(); iter != _pools.end(); ++iter)
    {
        iter->second->Show();
    }
}

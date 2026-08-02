#pragma once

#include "ecs/disposable.h"
#include "ecs/component.h"

class IDynamicObjectPool:public IDisposable
{
public:
    virtual void Update() = 0;
    virtual void FreeObject(IComponent* pObj) = 0;
    virtual void Show() = 0;
};

#pragma once
#include "ecs/component.h"
#include "ecs/system.h"
#include <functional>

using UpdateCallBackFun = std::function<void()>;
class UpdateComponent :public Component<UpdateComponent>, public IAwakeFromPoolSystem<UpdateCallBackFun>
{
public:
	void Awake(UpdateCallBackFun fun) override;
    void BackToPool() override;
    void Update() const;

private:
    UpdateCallBackFun _function{ nullptr };
};


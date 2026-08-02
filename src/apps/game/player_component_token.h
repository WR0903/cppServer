#pragma once
#include "libserver/ecs/component.h"
#include "libserver/ecs/system.h"

class PlayerComponentToken :public Component<PlayerComponentToken>, public IAwakeFromPoolSystem<std::string>
{
public:
	void Awake(const std::string token) override;
	void BackToPool() override;

	bool IsTokenValid(std::string token) const;

private:
	std::string _token;
};


#pragma once

#include "robot_state.h"
#include <chrono>

class RobotStateGameConnecting : public RobotState
{
public:
	DynamicStateCreate(RobotStateGameConnecting, RobotStateType::Game_Connecting);

    void OnEnterState() override;
	RobotStateType OnUpdate() override;
};

class RobotStateGameConnected : public RobotState
{
public:
	DynamicStateCreate(RobotStateGameConnected, RobotStateType::Game_Connected);

	void OnEnterState() override;
};

class RobotStateGameLogined : public RobotState
{
public:
	DynamicStateCreate(RobotStateGameLogined, RobotStateType::Game_Logined);
};

class RobotStateSpaceEnterWorld : public RobotState
{
public:
    DynamicStateCreate(RobotStateSpaceEnterWorld, RobotStateType::Space_EnterWorld);
    RobotStateType OnUpdate() override;
};

class RobotStateSpaceRoaming : public RobotState
{
public:
    DynamicStateCreate(RobotStateSpaceRoaming, RobotStateType::Space_Roaming);
    void OnEnterState() override;
    RobotStateType OnUpdate() override;

private:
    std::chrono::steady_clock::time_point _lastMoveTime;
    float _moveRange{ 30.0f };       // 随机移动范围（相对初始位置）
    int _moveIntervalSec{ 3 };       // 移动间隔（秒）
};
#pragma once

#include "libserver/utils/state_template.h"
#include "libserver/utils/robot_state_type.h"
#include "libserver/utils/vector3.h"

#include "libplayer/player.h"

#include "robot_state.h"

class Robot : public Player, public StateTemplateMgr<RobotStateType, RobotState, Robot>, virtual public IAwakeFromPoolSystem<std::string>
{
public:
    void Awake(std::string account) override;
    void BackToPool() override;
    void Update();
    void NetworkDisconnect();
    void EnterWorld(int worldId);

    Vector3 GetPosition() const;
    void SetPosition(const Vector3& pos);

protected:
    void RegisterState() override;

private:
    int _worldId{ 0 };
    Vector3 _currentPos{ 0, 0, 0 };
};


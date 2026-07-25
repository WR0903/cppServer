#include "robot_state_game.h"
#include "robot.h"
#include "libserver/message_system_help.h"
#include "robot_component_gametoken.h"
#include "libserver/log4_help.h"
#include "global_robots.h"

#include <chrono>
#include <random>

void RobotStateGameConnecting::OnEnterState()
{
    _pParentObj->NetworkDisconnect();

    // 请求一个新的Tcp连接
    auto tokenObj = _pParentObj->GetComponent<RobotComponentGameToken>();
    TagValue tagValue{ _pParentObj->GetAccount(), 0 };
    MessageSystemHelp::CreateConnect(NetworkType::TcpConnector, TagType::Account, tagValue, tokenObj->GetGameIp(), tokenObj->GetGamePort());
}

RobotStateType RobotStateGameConnecting::OnUpdate()
{
    auto socketKey = _pParentObj->GetSocketKey();
    if (socketKey->Socket != INVALID_SOCKET && socketKey->NetType == NetworkType::TcpConnector)
    {
        return RobotStateType::Game_Connected;
    }

    return GetState();
}

void RobotStateGameConnected::OnEnterState()
{
    auto tokenObj = _pParentObj->GetComponent<RobotComponentGameToken>();

    Proto::LoginByToken protoLogin;
    protoLogin.set_account(_pParentObj->GetAccount().c_str());
    protoLogin.set_token(tokenObj->GetToken().c_str());

    MessageSystemHelp::SendPacket(Proto::MsgId::C2G_LoginByToken, protoLogin, _pParentObj);
}

RobotStateType RobotStateSpaceEnterWorld::OnUpdate()
{
    // 进入世界后，切换到随机移动状态
    return RobotStateType::Space_Roaming;
}

void RobotStateSpaceRoaming::OnEnterState()
{
    _lastMoveTime = std::chrono::steady_clock::now();
}

RobotStateType RobotStateSpaceRoaming::OnUpdate()
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - _lastMoveTime).count();

    if (elapsed < _moveIntervalSec)
        return GetState();

    _lastMoveTime = now;

    // 在初始位置附近随机生成目标坐标
    auto pos = _pParentObj->GetPosition();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(-_moveRange, _moveRange);

    float targetX = pos.X + dis(gen);
    float targetZ = pos.Z + dis(gen);

    // 发送移动协议（单点路径）
    Proto::Move protoMove;
    auto pPos = protoMove.add_position();
    pPos->set_x(targetX);
    pPos->set_y(pos.Y);
    pPos->set_z(targetZ);

    MessageSystemHelp::SendPacket(Proto::MsgId::C2S_Move, protoMove, _pParentObj);

    if (GlobalRobots::GetInstance()->GetRobotsCount() == 1)
    {
        LOG_DEBUG("robot move. account:" << _pParentObj->GetAccount().c_str()
            << " target:(" << targetX << ", " << pos.Y << ", " << targetZ << ")");
    }

    return GetState();
}


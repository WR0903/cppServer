#pragma once

#include "network/network.h"

class NetworkListen :public Network,
    virtual public IAwakeSystem<std::string, int>,
    virtual public IAwakeSystem<int, int>
{
public:
    void Awake(std::string ip, int port) override;
    void Awake(int appType, int appId) override;
    void Awake(std::string ip, int port, NetworkType iType);

    void BackToPool() override;

    virtual void Update();
    const char* GetTypeName() override;
    uint64 GetTypeHashCode() override;
    void CmdShow();

private:
    void HandleListenKey(Packet* pPacket);

protected:
    virtual int Accept();
    virtual void OnEpoll(SOCKET fd, int index) override;

private:
    int _mainSocketEventIndex{ -1 };
    SOCKET _masterSocket{ INVALID_SOCKET };
};

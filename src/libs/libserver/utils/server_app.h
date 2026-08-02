#pragma once
#include "ecs/disposable.h"
#include "utils/common.h"
#include "thread/thread_mgr.h"
#include "utils/app_type.h"

#if ENGINE_PLATFORM != PLATFORM_WIN32
#include <signal.h>
#else
#include <csignal>
#endif

class ServerApp :public Singleton<ServerApp>, public IDisposable
{
public:
    ServerApp(APP_TYPE appType, int argc, char* argv[]);

    void Initialize();
    void Dispose() override;

    void Run();

    // signal
    static void Signalhandler(int signalValue);

protected:
    ThreadMgr* _pThreadMgr{ nullptr };
    APP_TYPE _appType{ APP_TYPE::APP_None };
    int _appId{ 0 };

    int _argc;
    char** _argv;
};


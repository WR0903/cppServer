#include "libserver/utils/common.h"
#include "libserver/utils/server_app.h"
#include "game.h"
#include "libserver/utils/global.h"
#include "libserver/network/network_listen.h"
#include "libserver/network/network_connector.h"
#include "libresource/resource_manager.h"
#include "libserver/message/message_system.h"

int main(int argc, char* argv[])
{
    const APP_TYPE curAppType = APP_TYPE::APP_GAME;
    ServerApp app(curAppType, argc, argv);
    app.Initialize();

    auto pThreadMgr = ThreadMgr::GetInstance();

    // 全局
    pThreadMgr->GetEntitySystem()->AddComponent<ResourceManager>();

    InitializeComponentGame(pThreadMgr);

    const auto pGlobal = Global::GetInstance();
    pThreadMgr->CreateComponent<NetworkListen>(ListenThread, false, (int)pGlobal->GetCurAppType(), (int)pGlobal->GetCurAppId());

    // connector
    pThreadMgr->CreateComponent<NetworkConnector>(ConnectThread, false,( int)NetworkType::TcpConnector, (int)(APP_APPMGR | APP_DB_MGR | APP_SPACE));

    app.Run();
    app.Dispose();

    return 0;
}

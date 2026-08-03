#pragma once

#include <map>
#include "utils/common.h"
#include "ecs/entity.h"
#include "cache/cache_swap.h"
#include "network/network_help.h"
#include "network/connect_obj.h"
#include "utils/trace_component.h"

#define MAX_CLIENT  5120

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> 

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

#include <sys/epoll.h>

#define _sock_init( )
#define _sock_nonblock( sockfd ) { int flags = fcntl(sockfd, F_GETFL, 0); fcntl(sockfd, F_SETFL, flags | O_NONBLOCK); }
#define _sock_accept( listen_fd, addr, len ) ::accept4(listen_fd, addr, len, SOCK_NONBLOCK )
#define _sock_accepted_nonblock( sockfd )    ((void)0)
#define _sock_exit( )
#define _sock_err( )	errno
#define _sock_close( sockfd ) ::close( sockfd ) 
#define _sock_is_blocked()	(errno == EAGAIN || errno == 0)

#define RemoveConnectObj(socket) \
    _connects[socket]->ComponentBackToPool( ); \
    _connects[socket] = nullptr; \
    DeleteEvent(_epfd, socket); \
    _sockets.erase(socket); 

#define SetsockOptType void *

class Packet;

class Network : public Entity<Network>, public INetwork
#if LOG_TRACE_COMPONENT_OPEN
    , public CheckTimeComponent
#endif
{
public:
    void BackToPool() override;
    void SendPacket(Packet*& pPacket) override;
    NetworkType GetNetworkType() const { return _networkType; }

protected:
    void SetSocketOpt(SOCKET socket);
    SOCKET CreateSocket();
    bool CheckSocket(SOCKET socket);
    bool CreateConnectObj(SOCKET socket, TagType tagType, TagValue tagValue, ConnectStateType iState);

    void HandleDisconnect(Packet* pPacket);

    void InitEpoll();
    void Epoll();
    void AddEvent(int epollfd, int fd, int flag);
    void ModifyEvent(int epollfd, int fd, int flag);
    void DeleteEvent(int epollfd, int fd);
    virtual void OnEpoll(SOCKET fd, int index) { };

    void OnNetworkUpdate();

protected:
    ConnectObj* _connects[MAX_CLIENT]{};
    std::set<SOCKET> _sockets;

#define MAX_EVENT   5120
    struct epoll_event _events[MAX_EVENT];
    int _epfd{ -1 };

    std::mutex _sendMsgMutex;
    CacheSwap<Packet> _sendMsgList;

    NetworkType _networkType{ NetworkType::TcpListen };
};

## 一、项目总览

代码链接：https://github.com/WR0903/cppServer

### 目录结构

```plain&#x20;text
cppServer/
├── engine.sln                  # Visual Studio 解决方案
├── make-all.sh                 # Linux 编译脚本
├── [[bin/                        # 输出目录
└── src/
    ├── libs/libserver/         # 核心服务器引擎库（60 个文件）
    ├── apps/login/    ](url)](url)         # 登录服务器应用
    └── tools/robots/           # 压测机器人工具
```

## 二、启动流程与整体架构

### 2.1 应用启动模板

所有应用通过 `MainTemplate` 统一启动：

```c++
// server_app.h:12-20
template<class APPClass>
inline int MainTemplate() {
    APPClass* pApp = new APPClass();
    pApp->InitApp();
    pApp->Run();
    delete pApp;
    return 0;
}
```

以 `login` 为例：

```c++
// apps/login/main.cpp
int main(int argc, char *argv[]) {
    return MainTemplate<LoginApp>();
}

// apps/login/login_app.cpp
void LoginApp::InitApp() {
    AddListenerToThread(”127.0.0.1“, 2233);   // 1. 创建监听网络
    _pThreadMgr->AddObjToThread(new RobotTest()); // 2. 加入业务对象
    _pThreadMgr->AddObjToThread(new Account());
    _pThreadMgr->AddObjToThread(new Console());
}
```

### 2.2 ServerApp 构造与运行

`ServerApp` 构造时完成全局单例初始化与线程池创建：

```c++
// server_app.cpp:6-25
ServerApp::ServerApp(APP_TYPE appType) {
    signal(SIGINT, Signalhandler);          // 注册信号处理
    _appType = appType;
    DynamicObjectPoolMgr::Instance();        // 对象池管理器
    Global::Instance();                      // 全局状态
    ThreadMgr::Instance();                   // 线程管理器
    _pThreadMgr = ThreadMgr::GetInstance();
    UpdateTime();

    for (int i = 0; i < 3; i++) {            // 默认创建 3 个工作线程
        _pThreadMgr->NewThread();
    }
    _pThreadMgr->StartAllThread();
}
```

主循环 `Run()` 负责：更新时间 → 主线程 `Update`（分发消息）→ 对象池 `Update`（回收对象），收到停止信号后按 ”停线程 → 回收线程资源 → 回收主线程资源 → 销毁对象池“ 顺序优雅退出。

### 2.3 架构分层

```plain&#x20;text
┌─────────────────────────────────────────────┐
│              ServerApp (主线程)              │
│   Run(): 时间更新 + 消息分发 + 对象池回收     │
├─────────────────────────────────────────────┤
│              ThreadMgr (单例)                │
│   管理多个 Thread，路由 Packet 与 Network     │
├──────────┬──────────┬──────────┬────────────┤
│ Thread 1 │ Thread 2 │ Thread 3 │  ...        │
│ ┌──────┐ │ ┌──────┐ │ ┌──────┐ │             │
│ │Network│ │ │Object│ │ │Object│ │  每个线程   │
│ │Listen │ │ │      │ │ │      │ │  独立事件循环│
│ └──────┘ │ └──────┘ │ └──────┘ │             │
│ + Objects│ + Objects│ + Objects│             │
└──────────┴──────────┴──────────┴────────────┘
```

## 三、多线程模块

### 3.1 核心类关系

```plain&#x20;text
ThreadObject (thread_obj.h)        ← 线程对象基类（Actor 雏形）
  └─ MessageList (message_list.h)  ← 消息回调注册
  └─ SnObject (sn_object.h)        ← 全局唯一 SN

ThreadObjectList (thread.h)        ← 线程内对象与消息容器
  ├─ CacheRefresh<ThreadObject>    ← 对象增删双缓存
  └─ CacheSwap<Packet>             ← 消息读写双缓存

Thread (thread.h)                  ← 线程，继承 ThreadObjectList + SnObject
ThreadMgr (thread_mgr.h)           ← 单例，管理所有线程
```

### 3.2 Thread —— 线程封装

`Thread` 封装 `std::thread`，启动后进入事件循环：

```c++
// thread.cpp:93-108
void Thread::Start() {
    _thread = std::thread([this]() {
        _state = ThreadState_Run;
        while (!Global::GetInstance()->IsStop) {
            Update();                                        // 处理消息与对象
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        _state = ThreadState_Stoped;
    });
}
```

线程状态机：`ThreadState_Init → ThreadState_Run → ThreadState_Stoped`。停止分两阶段：先置 `IsStop` 让循环退出（`IsStop`），再 `join()`（`IsDispose`）。

### 3.3 ThreadObjectList —— 线程内调度核心

这是单线程内对象与消息调度的核心，采用**双缓存无锁读**设计：

```c++
// thread.h:20-36
class ThreadObjectList : public IDisposable {
protected:
    std::mutex _obj_lock;
    CacheRefresh<ThreadObject> _objlist;    // 对象增删缓存
    std::mutex _packet_lock;
    CacheSwap<Packet> _cachePackets;        // 消息读写缓存
};
```

`Update()` 是每个线程每帧的执行逻辑：

```c++
// thread.cpp:26-71 (关键流程)
void ThreadObjectList::Update() {
    // 1. 交换对象增删缓存，回收已删除对象
    if (_objlist.CanSwap()) {
        auto pDelList = _objlist.Swap();
        for (auto pOne : pDelList) { pOne->Dispose(); delete pOne; }
    }
    // 2. 交换消息缓存（写 → 读）
    if (_cachePackets.CanSwap()) { _cachePackets.Swap(); }
    // 3. 遍历所有对象，分发消息并 Update
    auto pList = _objlist.GetReaderCache();
    auto pMsgList = _cachePackets.GetReaderCache();
    for (auto pObj : *pList) {
        for (auto pPacket : *pMsgList) {
            if (pObj->IsFollowMsgId(pPacket))
                pObj->ProcessPacket(pPacket);   // 仅处理关心的消息
        }
        pObj->Update();
        if (!pObj->IsActive())
            _objlist.GetRemoveCache()->emplace_back(pObj);
    }
    pMsgList->clear();
}
```

**设计要点**：

* 锁仅保护”写入/交换“瞬间，**读取与处理完全无锁**，降低竞争。

* 消息按 `IsFollowMsgId` 过滤后分发，实现”订阅式“消息投递。

* 非激活对象（`IsActive()==false`）自动回收。

### 3.4 ThreadMgr —— 线程管理

`ThreadMgr` 是单例，负责线程创建、对象分配、消息路由：

```c++
// thread_mgr.cpp:30-62  轮询分配对象到线程
bool ThreadMgr::AddObjToThread(ThreadObject* obj) {
    std::lock_guard<std::mutex> guard(_thread_lock);
    auto iter = _threads.begin();
    if (_lastThreadSn > 0)
        iter = _threads.find(_lastThreadSn);
    // 取下一个活动线程（轮询）
    do {
        ++iter;
        if (iter == _threads.end()) iter = _threads.begin();
    } while (!(iter->second->IsRun()));
    iter->second->AddObject(obj);
    _lastThreadSn = iter->second->GetSN();   // 记住上次分配位置
    return true;
}
```

**消息分发**有两种方式：

```c++
// thread_mgr.cpp:126-144
void ThreadMgr::DispatchPacket(Packet* pPacket) {
    AddPacketToList(pPacket);                 // 主线程
    for (auto& pair : _threads)              // 所有子线程（广播）
        pair.second->AddPacketToList(pPacket);
}

void ThreadMgr::SendPacket(Packet* pPacket) {
    NetworkListen* pLocator = static_cast<NetworkListen*>(GetNetwork(APP_Listen));
    pLocator->SendPacket(pPacket);           // 投递到网络层发送
}
```

* `DispatchPacket`：**广播**到所有线程（主+子），由各对象自行过滤。

* `SendPacket`：定向投递到监听网络的发送队列。

### 3.5 ThreadObject —— 线程对象（Actor 雏形）

```c++
// thread_obj.h:7-22
class ThreadObject : public MessageList, public SnObject {
public:
    virtual bool Init() = 0;                 // 初始化
    virtual void RegisterMsgFunction() = 0;  // 注册消息处理
    virtual void Update() = 0;               // 每帧逻辑
    void SetThread(Thread* pThread);
    bool IsActive() const;
protected:
    bool _active{ true };
    Thread* _pThread{ nullptr };
};
```

生命周期：`AddObject` 时调用 `Init()` → `RegisterMsgFunction()` → 每帧 `Update()` + 消息处理 → `Dispose()` 后被回收。

## 四、网络 Epoll / Reactor 模块

### 4.1 类继承体系

```plain&#x20;text
ThreadObject
  └─ Network (network.h)              ← 网络基类，IO 多路复用
       ├─ NetworkListen               ← 服务端监听 + Accept
       └─ NetworkConnector            ← 客户端连接 + 断线重连

ConnectObj (connect_obj.h)            ← 单连接对象（池化）
  ├─ RecvNetworkBuffer               ← 接收环形缓冲
  └─ SendNetworkBuffer               ← 发送环形缓冲
```

### 4.2 平台 IO 抽象

`network.h` 用宏统一封装 socket 操作：

```c++
// network.h:27-43 (Linux 分支)
#define _sock_init( )
#define _sock_nonblock( sockfd ) { int flags = fcntl(sockfd, F_GETFL, 0); \
                                   fcntl(sockfd, F_SETFL, flags | O_NONBLOCK); }
#define _sock_close( sockfd ) ::shutdown( sockfd, SHUT_RDWR )
#define _sock_is_blocked() (errno == EAGAIN || errno == 0)

// Windows 分支
#define _sock_init( ) { WSADATA wsaData; WSAStartup( MAKEWORD(2, 2), &wsaData ); }
#define _sock_nonblock( sockfd ) { unsigned long param = 1; \
                                   ioctlsocket(sockfd, FIONBIO, (unsigned long *)&param); }
#define _sock_is_blocked() (WSAGetLastError() == WSAEWOULDBLOCK)
```

通过编译宏 `EPOLL` 切换 IO 模型：

```c++
// network.h:63-71
#ifdef EPOLL
    void InitEpoll(); void Epoll();
    void AddEvent(...); void ModifyEvent(...); void DeleteEvent(...);
#else
    void Select();
#endif
```

### 4.3 Epoll 实现（Reactor 核心）

#### 4.3.1 初始化与事件注册

```c++
// network.h:82-90
#ifdef EPOLL
#define MAX_CLIENT  5120
#define MAX_EVENT   5120
    struct epoll_event _events[MAX_EVENT];
    int _epfd;
    int _mainSocketEventIndex{ -1 };
#endif

// network.cpp:148-152  创建 epoll 并注册主 socket
void Network::InitEpoll() {
    _epfd = epoll_create(MAX_CLIENT);
    AddEvent(_epfd, _masterSocket, EPOLLIN | EPOLLOUT | EPOLLRDHUP);
}
```

事件增删改封装：

```c++
// network.cpp:125-146
void Network::AddEvent(int epollfd, int fd, int flag) {
    struct epoll_event ev;
    ev.events = flag; ev.data.fd = fd;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
}
void Network::ModifyEvent(int epollfd, int fd, int flag) { /* EPOLL_CTL_MOD */ }
void Network::DeleteEvent(int epollfd, int fd)            { /* EPOLL_CTL_DEL */ }
```

#### 4.3.2 Epoll 事件循环（Reactor dispatch）

```c++
// network.cpp:154-200
void Network::Epoll() {
    _mainSocketEventIndex = -1;
    const int nfds = epoll_wait(_epfd, _events, MAX_EVENT, 0); // 非阻塞
    for (int index = 0; index < nfds; index++) {
        int fd = _events[index].data.fd;
        if (fd == _masterSocket) _mainSocketEventIndex = index; // 标记主 socket 事件
        auto iter = _connects.find(fd);
        if (iter == _connects.end()) continue;

        // 错误/断开 → 移除连接
        if (_events[index].events & EPOLLRDHUP || ... & EPOLLERR || ... & EPOLLHUP) {
            RemoveConnectObj(iter); continue;
        }
        // 可读 → 接收
        if (_events[index].events & EPOLLIN) {
            if (!iter->second->Recv()) { RemoveConnectObj(iter); continue; }
        }
        // 可写 → 发送
        if (_events[index].events & EPOLLOUT) {
            if (!iter->second->Send()) { RemoveConnectObj(iter); continue; }
            ModifyEvent(_epfd, iter->first, EPOLLIN | EPOLLRDHUP); // 发完切回只读
        }
    }
}
```

**Reactor 模式体现**：

* `epoll_wait` 作为**事件多路分解器（Demultiplexer）**

* 每个 `ConnectObj` 是**事件处理器（EventHandler）**

* `Epoll()` 是**分发器（Dispatcher）**，将就绪事件路由到对应 `ConnectObj` 的 `Recv()/Send()`

#### 4.3.3 EPOLLET 边沿触发

新连接注册时使用 `EPOLLET`（边沿触发）：

```c++
// network.cpp:113-115
void Network::CreateConnectObj(SOCKET socket) {
    ConnectObj* pConnectObj = DynamicObjectPool<ConnectObj>::GetInstance()->MallocObject(this, socket);
    _connects[socket] = pConnectObj;
#ifdef EPOLL
    AddEvent(_epfd, socket, EPOLLIN | EPOLLET | EPOLLRDHUP);
#endif
}
```

### 4.4 NetworkListen —— 服务端监听

```c++
// network_listen.cpp:82-94  epoll 模式 Update
void NetworkListen::Update() {
    Epoll();                              // 1. 处理 IO 事件
    if (_mainSocketEventIndex >= 0)       // 2. 主 socket 有事件 → Accept
        Accept();
    Network::Update();                    // 3. 处理待发送队列
}
```

`Accept` 循环接收直到返回 `INVALID_SOCKET`（非阻塞）：

```c++
// network_listen.cpp:46-64
int NetworkListen::Accept() {
    while (true) {
        const SOCKET socket = ::accept(_masterSocket, &socketClient, &socketLength);
        if (socket == INVALID_SOCKET) return rs;
        SetSocketOpt(socket);
        CreateConnectObj(socket);
        ++rs;
    }
}
```

### 4.5 NetworkConnector —— 客户端连接

支持**断线重连**：每帧检测 `INVALID_SOCKET` 则重新 `Connect`，连接成功通过 `EPOLLIN/EPOLLOUT` 事件触发 `TryCreateConnectObj`：

```c++
// network_connector.cpp:70-101
void NetworkConnector::Update() {
    if (_masterSocket == INVALID_SOCKET) {           // 断线重连
        if (!Connect(_ip, _port)) return;
    }
    Epoll();
    if (!IsConnected() && _mainSocketEventIndex >= 0) {
        if (_events[_mainSocketEventIndex].events & EPOLLIN ||
            _events[_mainSocketEventIndex].events & EPOLLOUT)
            TryCreateConnectObj();                    // connect 成功
    }
    Network::Update();
}
```

### 4.6 ConnectObj —— 连接对象（池化）

`ConnectObj` 继承 `ObjectBlock`，从对象池分配。每个连接含独立的收发环形缓冲：

```c++
// connect_obj.h:16-41
class ConnectObj : public ObjectBlock {
protected:
    Network* _pNetWork{ nullptr };
    SOCKET _socket;
    RecvNetworkBuffer* _recvBuffer{ nullptr };   // 接收缓冲
    SendNetworkBuffer* _sendBuffer{ nullptr };   // 发送缓冲
};
```

**接收流程**（`Recv`）：循环 `recv` 直到 `EAGAIN/EWOULDBLOCK` → 从缓冲解析 `Packet` → 广播到线程：

```c++
// connect_obj.cpp:63-138 (关键部分)
bool ConnectObj::Recv() const {
    while (true) {
        const int dataSize = ::recv(_socket, pBuffer, emptySize, 0);
        if (dataSize > 0) _recvBuffer->FillDate(dataSize);
        else if (dataSize == 0) break;          // 对端关闭
        else { /* EAGAIN/EWOULDBLOCK → isRs=true */ break; }
    }
    if (isRs) {
        while (auto pPacket = _recvBuffer->GetPacket()) {
            if (_pNetWork->IsBroadcast())
                ThreadMgr::GetInstance()->DispatchPacket(pPacket); // 广播
            else
                _pNetWork->GetThread()->AddPacketToList(pPacket);  // 单线程
        }
    }
    return isRs;
}
```

**发送流程**：`Network::Update` 从 `_sendMsgList` 取出待发 `Packet` → `ConnectObj::SendPacket` 写入发送缓冲 → `epoll` 可写事件触发 `Send`。

### 4.7 Socket 选项配置

```c++
// network.cpp:47-86
void Network::SetSocketOpt(SOCKET socket) {
    // 1. 端口复用
    setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, ...);
    // 2. 收发超时 3 秒
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, ...);
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, ...);
    // 3. Linux KeepAlive：空闲 120s 开始探测，间隔 10s，5 次
    setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, ...);
    setsockopt(socket, SOL_TCP, TCP_KEEPIDLE, ...);
    setsockopt(socket, SOL_TCP, TCP_KEEPINTVL, ...);
    setsockopt(socket, SOL_TCP, TCP_KEEPCNT, ...);
    // 4. 非阻塞
    _sock_nonblock(socket);
}
```

## 五、Protobuf 通信模块

### 5.1 Proto 定义

**协议 ID 枚举**（`proto_id.proto`）：

```protobuf
enum MsgId {
    None = 0;
    MI_NetworkConnect       = 1;   // 网络连接成功
    MI_NetworkListen        = 2;   // 监听到连接
    MI_NetworkDisconnect    = 3;   // 物理断开（network→上层）
    MI_NetworkDisconnectToNet = 5; // 逻辑断开（上层→network）
    MI_Ping                 = 101;
    C2L_AccountCheck        = 1001; // 验证账号
    C2L_AccountCheckRs      = 1002;
    MI_RobotSyncState       = 5001;
    // ...
}
```

**消息体**（`msg.proto`）：

```protobuf
message AccountCheck {
    string account = 1;
    string password = 2;
}
message AccountCheckRs {
    enum ReturnCode { ARC_OK = 0; ARC_NOT_FOUND_ACCOUNT = 2; ... }
    int32 return_code = 1;
}
```

编译脚本 `protobuf/build.sh` 与 `build.bat` 调用 `protoc` 生成 `msg.pb.h/.cc`、`proto_id.pb.h/.cc`。

### 5.2 Packet —— 协议封装

`Packet` 继承 `Buffer`，封装 protobuf 序列化/反序列化：

```c++
// packet.h:22-61
struct PacketHead { unsigned short MsgId; };   // 协议头（4 字节对齐）

class Packet : public Buffer {
    Proto::MsgId _msgId;
    SOCKET _socket;
public:
    template<class ProtoClass>
    ProtoClass ParseToProto() {                // 反序列化
        ProtoClass proto;
        proto.ParsePartialFromArray(GetBuffer(), GetDataLength());
        return proto;
    }
    template<class ProtoClass>
    void SerializeToBuffer(ProtoClass& proto) { // 序列化
        auto total = (unsigned int)proto.ByteSizeLong();
        while (GetEmptySize() < total) ReAllocBuffer();
        proto.SerializePartialToArray(GetBuffer(), total);
        FillData(total);
    }
};
```

默认缓冲 10KB，不足时按 `ADDITIONAL_SIZE`(128KB) 扩容。

### 5.3 网络数据包格式

收发采用统一的二进制帧格式：

```plain&#x20;text
┌──────────────┬──────────────┬──────────────────────┐
│ TotalSize    │ PacketHead   │ Protobuf Data        │
│ (ushort,2B)  │ (ushort,2B)  │ (变长)               │
└──────────────┴──────────────┴──────────────────────┘
```

* `TotalSizeType = unsigned short`（`network_buffer.h:18`）

* 发送时由 `SendNetworkBuffer::AddPacket` 拼装（`network_buffer.cpp:216-238`）

* 接收时由 `RecvNetworkBuffer::GetPacket` 解析（`network_buffer.cpp:120-169`）

接收解析会**校验 MsgId 合法性**，非法则关闭连接：

```c++
// network_buffer.cpp:147-154
const google::protobuf::EnumDescriptor *descriptor = Proto::MsgId_descriptor();
if (descriptor->FindValueByNumber(head.MsgId) == nullptr) {
    _pConnectObj->Close();        // 非法消息 → 关闭
    return nullptr;
}
```

### 5.4 消息回调系统（MessageList）

`MessageList` 提供消息注册与分发，是 Actor 模型的消息处理基础：

```c++
// message_list.h:18-30
class MessageCallBackFunction : public MessageCallBackFunctionInfo {
    using HandleFunction = std::function<void(Packet*)>;
    std::map<int, HandleFunction> _callbackHandle;   // msgId → handler
public:
    void RegisterFunction(int msgId, HandleFunction function);
    bool IsFollowMsgId(Packet* packet) override;      // 是否关注此消息
    void ProcessPacket(Packet* packet) override;       // 处理消息
};
```

**带对象过滤的回调**（`MessageCallBackFunctionFilterObj<T>`）：支持根据 `SOCKET` 查找业务对象后处理，用于多连接场景。

注册示例（`Network`）：

```c++
// network.cpp:34-39
void Network::RegisterMsgFunction() {
    auto pMsgCallBack = new MessageCallBackFunction();
    AttachCallBackHander(pMsgCallBack);
    pMsgCallBack->RegisterFunction(Proto::MsgId::MI_NetworkDisconnectToNet,
                                   BindFunP1(this, &Network::HandleDisconnect));
}
```

## 六、Actor 消息模型

本项目采用**轻量级 Actor 模型**：每个 `ThreadObject` 是一个 Actor，通过消息（`Packet`）进行线程间通信，无共享状态。

### 6.1 Actor 核心特征

| Actor 概念 | 本项目对应                                               |
| ——— | ————————————————— |
| Actor 实体 | `ThreadObject`（及其子类如 `Network`、`Account`、`Console`） |
| 消息       | `Packet`（含 MsgId + protobuf 体）                      |
| 邮箱       | `ThreadObjectList::_cachePackets`（双缓存）              |
| 消息处理     | `RegisterMsgFunction` 注册 + `ProcessPacket` 分发       |
| 调度器      | `Thread`（每个线程调度其内多个 Actor）                          |

### 6.2 消息流转

```plain&#x20;text
                  ┌─────────────────────────────────┐
                  │        收消息路径                 │
                  └─────────────────────────────────┘
   ConnectObj::Recv
        │
        ▼
   RecvNetworkBuffer::GetPacket  (解析出 Packet)
        │
        ├─ 广播模式: ThreadMgr::DispatchPacket → 所有线程 _cachePackets(写)
        └─ 定向模式: Thread::AddPacketToList  → 单线程 _cachePackets(写)
        │
        ▼ (下一帧)
   Thread::Update → CacheSwap::Swap → 遍历对象 → ProcessPacket(读,无锁)

                  ┌─────────────────────────────────┐
                  │        发消息路径                 │
                  └─────────────────────────────────┘
   ThreadObject::SendPacket (MessageList 静态方法)
        │
        ▼
   ThreadMgr::SendPacket → NetworkListen::SendPacket → _sendMsgList(写)
        │
        ▼ (下一帧)
   Network::Update → CacheSwap::Swap → ConnectObj::SendPacket → 发送缓冲 → epoll 发送
```

### 6.3 双缓存实现无锁读

**CacheSwap**（消息缓存）：读写两个 `list` 指针，写时入 writer，帧末 `Swap` 交换指针，读时访问 reader：

```c++
// cache_swap.h:43-48
inline void CacheSwap<T>::Swap() {
    auto tmp = _readerCache;
    _readerCache = _writerCache;
    _writerCache = tmp;
}
```

**CacheRefresh**（对象增删缓存）：维护 `_reader`/`_add`/`_remove` 三个 vector，`Swap` 时合并 add、移除 remove，返回待删除列表：

```c++
// cache_refresh.h:45-73
inline std::list<T*> CacheRefresh<T>::Swap() {
    std::list<T*> rs;
    for (auto one : _add) _reader.push_back(one);  // 合并新增
    _add.clear();
    for (auto one : _remove) {                      // 移除标记
        auto iter = std::find_if(...);
        if (iter != _reader.end()) { rs.push_back(one); _reader.erase(iter); }
    }
    _remove.clear();
    return rs;   // 返回待释放对象
}
```

**核心优势**：锁只持有极短时间（仅 push/swap），消息处理与对象遍历完全无锁。

### 6.4 线程间通信示例

业务对象（如 `Account`）发送消息：

```c++
// 任何 ThreadObject 中均可调用
MessageList::SendPacket(pPacket);
// → ThreadMgr::SendPacket → NetworkListen::SendPacket → 网络
```

网络收到消息后广播给所有业务 Actor：

```c++
// connect_obj.cpp:125-132
if (_pNetWork->IsBroadcast())
    ThreadMgr::GetInstance()->DispatchPacket(pPacket);  // 广播
```

## 七、内存池模块

### 7.1 整体设计

```plain&#x20;text
DynamicObjectPoolMgr (单例, object_pool_mgr.h)
  │  管理所有对象池，统一 Update/Dispose
  │
  ├─ DynamicObjectPool<ConnectObj>  (单例模板)
  ├─ DynamicObjectPool<Packet>
  └─ DynamicObjectPool<OtherType>...
        │
        ├─ _free: queue<T*>       空闲对象队列
        └─ _objInUse: CacheRefresh<T>  使用中对象(双缓存)
```

### 7.2 ObjectBlock —— 池化对象基类

```c++
// object_block.h:7-17
class ObjectBlock : virtual public SnObject, virtual public IDisposable {
public:
    ObjectBlock(IDynamicObjectPool* pPool);   // 记录所属池
    virtual void BackToPool() = 0;            // 子类实现：重置状态
    virtual void Dispose() override { BackToPool(); }
protected:
    IDynamicObjectPool* _pPool{ nullptr };
};
```

`Dispose()` 调用 `BackToPool()`，后者负责重置状态并调用 `_pPool->FreeObject(this)` 归还对象池。

### 7.3 DynamicObjectPool —— 模板对象池

```c++
// object_pool.h:15-69
template <typename T>
class DynamicObjectPool : public IDynamicObjectPool {
    std::queue<T*> _free;            // 空闲队列
    std::mutex _freeLock;
    CacheRefresh<T> _objInUse;       // 使用中(双缓存)
public:
    static DynamicObjectPool<T>* GetInstance();   // 单例，自动注册到 Mgr
    template<typename ...Targs>
    T* MallocObject(Targs... args);               // 分配
    void FreeObject(ObjectBlock* pObj);           // 回收
    void Update() override;                        // 合并回收队列
};
```

#### 7.3.1 分配对象

```c++
// object_pool.h:116-143
T* DynamicObjectPool<T>::MallocObject(Targs... args) {
    _freeLock.lock();
    if (_free.size() == 0) CreateOne();           // 空则新建
    auto pObj = _free.front();
    _free.pop();
    _freeLock.unlock();

    pObj->ResetSN();                              // 重置全局 SN
    pObj->TakeoutFromPool(std::forward<Targs>(args)...);  // 初始化

    _inUseLock.lock();
    _objInUse.GetAddCache()->push_back(pObj);     // 加入使用中
    _inUseLock.unlock();
    return pObj;
}
```

#### 7.3.2 回收对象

对象 `Dispose()` → `BackToPool()` → `FreeObject`：

```c++
// object_pool.h:163-168
void DynamicObjectPool<T>::FreeObject(ObjectBlock* pObj) {
    std::lock_guard<std::mutex> guard(_inUseLock);
    _objInUse.GetRemoveCache()->emplace_back(dynamic_cast<T*>(pObj));  // 标记移除
}
```

#### 7.3.3 定期合并

`DynamicObjectPoolMgr::Update` 每帧调用各池 `Update`，将 remove 缓存中的对象移回 `_free`：

```c++
// object_pool.h:145-161
void DynamicObjectPool<T>::Update() {
    std::list<T*> freeObjs;
    _inUseLock.lock();
    if (_objInUse.CanSwap()) freeObjs = _objInUse.Swap();  // 合并并取出已移除
    _inUseLock.unlock();
    std::lock_guard<std::mutex> guard(_freeLock);
    for (auto one : freeObjs) _free.push(one);             // 放回空闲
}
```

### 7.4 ConnectObj 池化示例

`Network` 创建连接时从池分配：

```c++
// network.cpp:102-104
ConnectObj* pConnectObj =
    DynamicObjectPool<ConnectObj>::GetInstance()->MallocObject(this, socket);
```

连接断开时回收：

```c++
// connect_obj.cpp:34-51
void ConnectObj::BackToPool() {
    if (!Global::GetInstance()->IsStop) {
        Packet* pResultPacket = new Packet(Proto::MsgId::MI_NetworkDisconnect, _socket);
        MessageList::DispatchPacket(pResultPacket);   // 通知上层
    }
    _pNetWork = nullptr;
    _socket = INVALID_SOCKET;
    _recvBuffer->BackToPool();
    _sendBuffer->BackToPool();
    _pPool->FreeObject(this);                         // 归还池
}
```

### 7.5 缓冲区管理（Buffer）

`Buffer` 基类提供动态扩容的线性缓冲，`NetworkBuffer` 扩展为环形缓冲：

```c++
// base_buffer.h:14-40
class Buffer {
protected:
    char* _buffer{ nullptr };
    unsigned int _beginIndex{ 0 };   // 数据起始
    unsigned int _endIndex{ 0 };     // 数据结束
    unsigned int _bufferSize{ 0 };   // 总容量
};
```

环形缓冲通过 `_dataSize` 记录有效数据量，解决”首尾重合“歧义：

```c++
// network_buffer.h:43-46
class NetworkBuffer : public Buffer {
protected:
    unsigned int _dataSize;          // 有效数据量
};
```

扩容策略：每次增加 `ADDITIONAL_SIZE`(128KB)，上限 `MAX_SIZE`(1MB)。

### 7.6 调试与监控

通过 `ConsoleCmdPool` 控制台命令查看对象池状态：

```c++
// console_cmd_pool.cpp:16-20
void ConsoleCmdPool::HandleShow(std::vector<std::string>& params) {
    DynamicObjectPool<ConnectObj>::GetInstance()->Show();  // 输出 free/inUse/totalCall
}
```

`Show()` 在 Debug 模式输出总数量、空闲数、使用数、累计分配次数。

## 八、全局辅助模块

### 8.1 Singleton —— 单例模板

```c++
// singleton.h:5-31
template <typename T>
class Singleton {
    static T* Instance(Args&&... args);   // 创建
    static T* GetInstance();              // 获取（未初始化抛异常）
    static void DestroyInstance();        // 销毁
};
```

用于 `Global`、`ThreadMgr`、`DynamicObjectPoolMgr` 等。

### 8.2 Global —— 全局状态与 SN 生成

```c++
// global.h:9-22
class Global : public Singleton<Global> {
public:
    uint64 GenerateSN();      // 生成全局唯一 ID
    int YearDay;
    timeutil::Time TimeTick;  // 毫秒时间戳
    bool IsStop{ false };     // 全局停止标志
private:
    unsigned int _snTicket{ 1 };
    unsigned int _serverId{ 0 };
};
```

SN 格式：`(TimeTick << 32) + (serverId << 16) + snTicket`，64 位，含时间、服务器、序号。

### 8.3 SnObject —— 全局唯一标识

```c++
// sn_object.h:6-29
class SnObject {
    SnObject() { _sn = Global::GetInstance()->GenerateSN(); }
    void ResetSN() { _sn = Global::GetInstance()->GenerateSN(); }  // 池化复用时重置
    uint64 GetSN() const { return _sn; }
};
```

`Thread`、`ThreadObject`、`ObjectBlock` 均继承此类，便于对象追踪与日志。

### 8.4 IDisposable —— 资源释放接口

```c++
// disposable.h:3-11
class IDisposable {
    virtual void Dispose() = 0;
};
```

所有需要资源管理的类统一实现此接口，由容器在销毁时统一调用。

## 九、关键设计总结

### 9.1 并发模型

| 层级  | 机制                       | 说明                |
| — | ———————— | —————— |
| 线程间 | 消息队列 + 双缓存               | 无共享状态，靠 Packet 通信 |
| 线程内 | 单线程事件循环                  | Update 驱动，无锁处理消息  |
| IO  | epoll(Linux)/select(Win) | 非阻塞 IO + Reactor  |

### 9.2 性能优化点

1. **双缓存无锁读**：`CacheSwap`/`CacheRefresh` 使锁仅保护瞬间写入，处理阶段无锁。

2. **对象池**：`ConnectObj` 等高频对象池化，避免频繁 new/delete。

3) **epoll 边沿触发**：`EPOLLET` 减少系统调用次数。

4) **环形缓冲**：收发缓冲环形设计，减少内存拷贝与扩容。

5. **轮询负载均衡**：`ThreadMgr` 轮询分配对象到线程，均衡负载。

### 9.3 优雅退出流程

```plain&#x20;text
1. 收到 SIGINT → Global::IsStop = true
2. 各 Thread 循环退出 → ThreadState_Stoped
3. ThreadMgr::IsStopAll() 确认所有线程停止
4. ThreadMgr::IsDisposeAll() join 所有线程
5. ThreadMgr::Dispose() 回收主线程对象
6. DynamicObjectPoolMgr::Update() 最后回收一次
7. DynamicObjectPoolMgr::Dispose() 销毁所有池
8. Global/ThreadMgr 销毁单例
```

### 9.4 扩展性

* **新增应用**：继承 `ServerApp`，实现 `InitApp` 添加监听与业务对象。

* **新增业务 Actor**：继承 `ThreadObject`，实现 `Init/RegisterMsgFunction/Update`，通过 `AddObjToThread` 加入线程。

* **新增协议**：在 `.proto` 中定义消息与 MsgId，`protoc` 生成代码后注册回调。

* **新增池化对象**：继承 `ObjectBlock`，实现 `BackToPool`，用 `DynamicObjectPool<T>` 管理。

## 十、模块依赖关系图

```plain&#x20;text
                    ┌──────────┐
                    │ ServerApp│
                    └────┬─────┘
            ┌───────────┼───────────┐
            ▼           ▼           ▼
       ┌────────┐  ┌────────┐  ┌────────────┐
       │ThreadMgr│  │ Global │  │PoolMgr     │
       └────┬────┘  └────────┘  └─────┬──────┘
            │                           │
       ┌────┴────┐               ┌─────┴──────┐
       │ Thread  │               │ObjectPool<T>│
       │  (多个) │               └─────┬──────┘
       └────┬────┘                     │
            │                     ┌────┴────┐
       ┌────┴──────────┐          │ObjectBlock│
       │ThreadObjectList│         └────┬────┘
       │ (对象+消息缓存) │              │
       └────┬──────────┘         ┌─────┴─────┐
            │                    │ ConnectObj │
   ┌────────┼────────┐           └─────┬─────┘
   ▼        ▼        ▼                 │
┌─────┐ ┌─────┐ ┌─────────┐       ┌────┴─────┐
│Net- │ │Net- │ │ 业务对象 │       │Recv/Send │
│Listen│ │Conn │ │(Account)│       │ Buffer   │
└──┬──┘ └─────┘ └─────────┘       └──────────┘
   │
   ▼  epoll/select
┌──────────────────┐
│ Packet + Protobuf│
└──────────────────┘
```


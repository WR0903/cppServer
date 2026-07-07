# C++ 多线程网络服务器架构解析

> 源码地址：[https://github.com/WR0903/cppServer](https://github.com/WR0903/cppServer)

—

## 一、项目全貌

### 目录结构

```
cppServer/
├── engine.sln                   # Visual Studio 解决方案
├── make-all.sh                  # Linux 编译脚本
├── bin/                         # 输出目录
└── src/
    ├── libs/libserver/          # 核心服务器库（ECS + 网络 + 内存池）
    ├── apps/login/              # 登录服务器（业务示例）
    └── tools/robots/            # 压测机器人
```

### 核心文件一览

| 文件 | 职责 |
|——|——|
| `entity_system.h/cpp` | 服务器核心——管理组件生命周期与调度 |
| `thread_mgr.h/cpp` | 主线程服务器 + 线程管理器（单例） |
| `thread.h/cpp` | 工作线程，每个线程即一个独立的子服务器 |
| `component.h/cpp` | 组件基类 `IComponent` / `Component<T>` |
| `entity.h/cpp` | 实体基类 `IEntity` / `Entity<T>`（可挂载子组件） |
| `system.h` | System 接口族：Awake / Update / Message |
| `network.h/cpp` | 网络实体——epoll/select IO 多路复用 |
| `connect_obj.h/cpp` | 连接对象（池化），含环形收发缓冲 |
| `object_pool.h` | 模板对象池 + CacheRefresh 双缓存回收 |
| `component_factory.h` | 反射工厂——按类名字符串动态创建组件 |
| `create_component.h/cpp` | 远程组件创建——协议驱动跨线程创建 |
| `cache_swap.h` | 双缓存容器（写入无阻塞读取） |

—

## 二、30 秒理解整体架构

```
┌─────────────────────────────────────────────────────────────┐
│  ServerApp::Run()  主循环                                     │
│    UpdateTime → ThreadMgr::Update → ObjectPoolMgr::Update   │
├─────────────────────────────────────────────────────────────┤
│               ThreadMgr（主服务器调度，单例）                    │
│  ┌──────────┬──────────┬──────────┐                         │
│  │ Thread 0 │ Thread 1 │ Thread 2 │  ... 每个都是子服务器        │
│  │ Network  │ Account  │ RobotTest│                         │
│  │ Listen   │ Component│ Component│                         │
│  └──────────┴──────────┴──────────┘                         │
├─────────────────────────────────────────────────────────────┤
│  线程间通信：消息队列 + 双缓存（CacheSwap）                      │
│  IO 模型：epoll (Linux) / select (Windows)                    │
│  内存管理：DynamicObjectPool 对象池                             │
└─────────────────────────────────────────────────────────────┘
```

**一句话总结**：每个线程运行一个独立的 EntitySystem（子服务器），线程间通过消息队列+双缓存通信，IO 层使用 epoll Reactor 模型，所有高频对象池化管理。

—

## 三、启动流程

### 3.1 启动模板

所有应用共用 `MainTemplate` 入口：

```cpp
template<class APPClass>
inline int MainTemplate() {
    APPClass* pApp = new APPClass();
    pApp->InitApp();
    pApp->Run();
    delete pApp;
    return 0;
}
```

### 3.2 ServerApp 构造

构造时完成三件事：注册信号、创建全局单例、启动工作线程。

```cpp
ServerApp::ServerApp(APP_TYPE appType) {
    signal(SIGINT, Signalhandler);       // 优雅退出信号
    
    DynamicObjectPoolMgr::Instance();     // 对象池管理器
    Global::Instance();                   // 全局状态 + SN 生成
    ThreadMgr::Instance();                // 主线程调度器

    _pThreadMgr = ThreadMgr::GetInstance();
    UpdateTime();

    for (int i = 0; i < 3; i++)           // 创建 3 个工作线程
        _pThreadMgr->NewThread();
    _pThreadMgr->StartAllThread();
}
```

### 3.3 业务初始化示例（LoginApp）

```cpp
void LoginApp::InitApp() {
    CreateComponent<NetworkListen>(“127.0.0.1”, 2233);  // 网络监听
    _pThreadMgr->CreateComponent<RobotTest>();           // 压测组件
    _pThreadMgr->CreateComponent<Account>();             // 账号验证
    _pThreadMgr->CreateComponent<Console>();             // 控制台
}
```

### 3.4 主循环

```
Run() {
    while (!IsStop) {
        UpdateTime();                       // 更新时间戳
        _pThreadMgr->Update();             // 服务器调度（消息分发 + 组件 Update）
        DynamicObjectPoolMgr::Update();    // 对象池回收
        sleep(1ms);
    }
    // 优雅退出（见第八章）
}
```

—

## 四、ECS 核心架构

### 4.1 两棵继承树

框架的核心设计是**两棵独立的继承树**，业务类通过多重继承组合它们：

```
继承树 A：身份与数据                   继承树 B：行为能力
─────────────────────────           ─────────────────────
SnObject                            ISystem
  └── IComponent                      ├── IAwakeFromPoolSystem<T...>
        ├── Component<T>              ├── IUpdateSystem
        └── IEntity                   └── IMessageSystem
              └── Entity<T>
```

**组合示例**：

```cpp
// Account：组件 + 池化初始化 + 消息处理
class Account : public Component<Account>,
                public IAwakeFromPoolSystem<>,
                public IMessageSystem { ... };

// Network：实体 + 池化初始化 + 消息处理
class Network : public Entity<Network>,
                public IAwakeFromPoolSystem<NetworkType, std::string, int>,
                public IMessageSystem { ... };

// Console：实体 + 池化初始化 + 每帧更新
class Console : public Entity<Console>,
                public IAwakeFromPoolSystem<>,
                public IUpdateSystem { ... };
```

### 4.2 IComponent —— 组件基类

```cpp
class IComponent : virtual public SnObject {
protected:
    bool _active{ true };              // 活跃标记，false 则下帧自动回收
private:
    IEntity* _parent{ nullptr };       // 所属实体
    EntitySystem* _pEntitySystem;      // 所属服务器（EntitySystem）
    IDynamicObjectPool* _pPool;        // 所属对象池
public:
    template<class T> T* GetParent();
    virtual void BackToPool() = 0;     // 归还对象池（子类实现）
};

template<class T>
class Component : public IComponent {
    virtual const char* GetTypeName();  // typeid 运行时类型名
    uint64 GetTypeHashCode();           // typeid hash（用作 map key）
};
```

### 4.3 IEntity —— 实体（可挂载子组件）

```cpp
class IEntity : public IComponent {
    std::map<uint64, IComponent*> _components;   // 子组件表
public:
    template<class T, typename... TArgs>
    void AddComponent(TArgs... args);            // 从对象池分配子组件

    template<class T>
    T* GetComponent();                           // 按类型查找
};
```

> **核心洞察**：`IEntity` 继承 `IComponent`，即实体本身也是组件。这使得服务器用统一接口管理一切。

### 4.4 System 接口族

| 接口 | 用途 | 触发时机 |
|——|——|-———|
| `IAwakeFromPoolSystem<T...>` | 从对象池取出时初始化 | `MallocObject` 后手动调用 |
| `IUpdateSystem` | 每帧更新 | 服务器 `Update` 自动调用 |
| `IMessageSystem` | 消息回调 | 收到匹配 MsgId 的 Packet 时 |

### 4.5 EntitySystem —— ECS 服务器调度核心

每个线程拥有一个 EntitySystem 实例，负责组件的**注册、调度、回收**。

**注册（AddToSystem）**—— 通过 `dynamic_cast` 自动识别组件能力：

```cpp
void EntitySystem::AddToSystem(IComponent* pComponent) {
    pComponent->SetEntitySystem(this);
    _objSystems[pComponent->GetSN()] = pComponent;

    // 自动识别能力并注册
    if (auto p = dynamic_cast<IUpdateSystem*>(pComponent))
        _updateSystems.push_back(p);

    if (auto p = dynamic_cast<IMessageSystem*>(pComponent)) {
        p->RegisterMsgFunction();    // 子类注册消息回调
        _messageSystems.push_back(p);
    }
}
```

**每帧调度（Update）**—— 严格”先消息后更新”：

```cpp
void EntitySystem::Update() {
    // 1. 消息分发
    UpdateMessage();  // 双缓存交换 → 遍历 _messageSystems → 按 MsgId 匹配处理

    // 2. 组件更新 + 自动回收
    for (auto it = _updateSystems.begin(); it != _updateSystems.end(); ) {
        auto pComp = dynamic_cast<IComponent*>(*it);
        if (!pComp->IsActive()) {
            _objSystems.erase(pComp->GetSN());
            it = _updateSystems.erase(it);
            pComp->ComponentBackToPool();    // 归还对象池
        } else {
            (*it)->Update();
            ++it;
        }
    }
}
```

—

## 五、线程模型

### 5.1 Thread —— 工作线程

每个 Thread 继承 EntitySystem，运行独立的事件循环：

```cpp
void Thread::Start() {
    _thread = std::thread([this]() {
        InitComponent();                     // 安装 CreateComponentC 基础组件
        _state = ThreadState_Run;
        while (!Global::GetInstance()->IsStop) {
            Update();                        // 服务器 Update（消息分发 + 组件更新）
            std::this_thread::sleep_for(1ms);
        }
        _state = ThreadState_Stoped;
    });
}
```

### 5.2 ThreadMgr —— 主线程调度中心

ThreadMgr 身兼两职：
1. **主线程的服务器**——处理主线程上的组件
2. **线程管理器**——将组件创建请求分发到工作线程

**跨线程组件创建流程**：

```
ThreadMgr::CreateComponent<T>(args...)
    │
    ├─ 1. 自动注册到 ComponentFactory（首次）
    ├─ 2. 参数序列化为 Protobuf 消息
    └─ 3. 写入 _createPackets 双缓存
         │
         ▼ (主线程下一帧 Update)
    ThreadMgr::Update()
    ├─ 交换双缓存
    └─ 轮询(round-robin)分发到 Thread[N]
         │
         ▼ (工作线程 UpdateMessage)
    CreateComponentC::HandleCreateComponent
    ├─ 反序列化参数
    ├─ ComponentFactory::Create(className, args...)
    └─ AddToSystem(newComponent)
```

**消息广播**：`DispatchPacket` 同时投递到主线程 + 所有子线程。

—

## 六、网络模块（Epoll Reactor）

### 6.1 类结构

```
Network（网络实体基类，IO 多路复用）
  ├── NetworkListen    服务端：监听 + Accept
  └── NetworkConnector 客户端：连接 + 断线重连

ConnectObj（池化连接对象）
  ├── RecvNetworkBuffer  接收环形缓冲
  └── SendNetworkBuffer  发送环形缓冲
```

### 6.2 Reactor 核心——Epoll 事件循环

```cpp
void Network::Epoll() {
    const int nfds = epoll_wait(_epfd, _events, MAX_EVENT, 0);  // 非阻塞
    for (int i = 0; i < nfds; i++) {
        int fd = _events[i].data.fd;
        
        if (fd == _masterSocket) { _mainSocketEventIndex = i; continue; }
        
        auto iter = _connects.find(fd);
        if (iter == _connects.end()) continue;

        // 错误/断开
        if (_events[i].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
            RemoveConnectObj(iter); continue;
        }
        // 可读 → 接收
        if (_events[i].events & EPOLLIN) {
            if (!iter->second->Recv()) { RemoveConnectObj(iter); continue; }
        }
        // 可写 → 发送
        if (_events[i].events & EPOLLOUT) {
            if (!iter->second->Send()) { RemoveConnectObj(iter); continue; }
            ModifyEvent(_epfd, fd, EPOLLIN | EPOLLRDHUP);  // 发完切回只读
        }
    }
}
```

**Reactor 三要素**：
- **事件多路分解器**：`epoll_wait`
- **事件处理器**：每个 `ConnectObj`
- **分发器**：`Epoll()` 方法将就绪事件路由到对应 ConnectObj

### 6.3 服务端监听（NetworkListen）

```cpp
void NetworkListen::Update() {
    Epoll();                               // 处理 IO 事件
    if (_mainSocketEventIndex >= 0)
        Accept();                          // 非阻塞循环 accept 直到 INVALID_SOCKET
    Network::Update();                     // 处理待发送队列
}
```

### 6.4 客户端连接（NetworkConnector）

支持断线重连：每帧检测连接状态，断开则重新 connect。

```cpp
void NetworkConnector::Update() {
    if (_masterSocket == INVALID_SOCKET) {
        if (!Connect(_ip, _port)) return;   // 尝试重连
    }
    Epoll();
    if (!IsConnected() && _mainSocketEventIndex >= 0)
        TryCreateConnectObj();              // 连接成功，创建 ConnectObj
    Network::Update();
}
```

### 6.5 ConnectObj 收发流程

**接收**：循环 `recv` 直到 `EAGAIN` → 从环形缓冲解析出 Packet → 分发到消息系统

```cpp
bool ConnectObj::Recv() {
    while (true) {
        int n = ::recv(_socket, pBuffer, emptySize, 0);
        if (n > 0) _recvBuffer->FillDate(n);
        else if (n == 0) return false;       // 对端关闭
        else break;                          // EAGAIN，稍后再读
    }
    // 解析并分发
    while (auto pPacket = _recvBuffer->GetPacket()) {
        if (_pNetWork->IsBroadcast())
            ThreadMgr::GetInstance()->DispatchPacket(pPacket);  // 广播
        else
            _pNetWork->GetThread()->AddPacketToList(pPacket);   // 定向
    }
    return true;
}
```

**发送**：业务层 `SendPacket` → 写入发送缓冲 → epoll EPOLLOUT 事件触发 → `Send()` 实际写出

### 6.6 Socket 配置

```cpp
void Network::SetSocketOpt(SOCKET socket) {
    setsockopt(..., SO_REUSEADDR, ...);     // 端口复用
    setsockopt(..., SO_SNDTIMEO, 3s);       // 收发超时
    setsockopt(..., SO_RCVTIMEO, 3s);
    setsockopt(..., SO_KEEPALIVE, ...);     // TCP KeepAlive
    setsockopt(..., TCP_KEEPIDLE, 120s);    // 空闲 120s 开始探测
    setsockopt(..., TCP_KEEPINTVL, 10s);    // 间隔 10s
    setsockopt(..., TCP_KEEPCNT, 5);        // 5 次无响应则断开
    _sock_nonblock(socket);                 // 非阻塞
}
```

—

## 七、协议与消息系统

### 7.1 数据包格式

```
┌──────────────┬──────────────┬──────────────────────┐
│ TotalSize    │ MsgId        │ Protobuf Payload     │
│ (2 bytes)    │ (2 bytes)    │ (变长)               │
└──────────────┴──────────────┴──────────────────────┘
```

### 7.2 Packet 封装

```cpp
class Packet : public Buffer {
    Proto::MsgId _msgId;
    SOCKET _socket;
public:
    template<class ProtoClass>
    ProtoClass ParseToProto();          // 反序列化 Protobuf

    template<class ProtoClass>
    void SerializeToBuffer(ProtoClass& proto);  // 序列化写入缓冲
};
```

### 7.3 消息注册与回调

```cpp
// 业务组件注册消息处理函数
void Account::RegisterMsgFunction() {
    auto pCallback = new MessageCallBackFunction();
    AttachCallBackHandler(pCallback);
    pCallback->RegisterFunction(
        Proto::MsgId::C2L_AccountCheck,
        BindFunP1(this, &Account::HandleAccountCheck)
    );
}
```

### 7.4 消息流转全景

```
                     ┌─── 收消息 ───┐
网络数据 → ConnectObj::Recv → 解析 Packet
    │
    ├─ 广播: DispatchPacket → 主线程 + 所有子线程的 _cachePackets
    └─ 定向: AddPacketToList → 目标线程的 _cachePackets
    │
    ▼ (下一帧)
服务器 UpdateMessage → Swap双缓存 → 遍历 _messageSystems → 匹配回调

                     ┌─── 发消息 ───┐
业务组件 → SendPacket → Network._sendMsgList
    │
    ▼ (下一帧)
Network::Update → ConnectObj::SendPacket → 发送缓冲 → epoll EPOLLOUT → 写出
```

### 7.5 双缓存（CacheSwap）

核心思路：**写端和读端各持一个 list，swap 时只交换指针**。

```cpp
template<class T>
class CacheSwap {
    std::list<T*>* _writerCache;   // 外部线程写入
    std::list<T*>* _readerCache;   // 当前线程读取
public:
    void Swap() {
        std::swap(_writerCache, _readerCache);
    }
};
```

**效果**：锁仅保护 push 和 swap 瞬间，消息处理阶段完全无锁。

—

## 八、内存池

### 8.1 架构

```
DynamicObjectPoolMgr（单例，管理所有池）
  ├── DynamicObjectPool<ConnectObj>
  ├── DynamicObjectPool<Packet>
  └── DynamicObjectPool<T>...
       ├── _free: queue<T*>          空闲对象
       └── _objInUse: CacheRefresh<T>  使用中（双缓存延迟回收）
```

### 8.2 分配流程

```cpp
T* MallocObject(Targs... args) {
    lock();
    if (_free.empty()) CreateOne();    // 空则新建
    auto obj = _free.front();
    _free.pop();
    unlock();

    obj->ResetSN();                    // 重置唯一标识
    obj->TakeoutFromPool(args...);     // AwakeFromPool 初始化
    
    _objInUse.Add(obj);                // 加入使用中
    return obj;
}
```

### 8.3 回收流程

```
组件 _active = false
  → 下帧服务器 Update 检测
  → ComponentBackToPool()
  → BackToPool()（子类重置状态）
  → FreeObject() 标记到 _objInUse 的 remove 缓存
  → 下帧 ObjectPoolMgr::Update → Swap 合并 → 归还 _free
```

### 8.4 环形缓冲区

收发数据使用环形缓冲，避免频繁内存拷贝：

```cpp
class NetworkBuffer : public Buffer {
    unsigned int _dataSize;       // 有效数据量（解决首尾重合歧义）
    // 扩容策略：每次 +128KB，上限 1MB
};
```

—

## 九、反射与动态创建

### 9.1 ComponentFactory

按类名字符串运行时创建组件：

```cpp
template<typename... Targs>
class ComponentFactory {
    std::map<std::string, std::function<IComponent*(Targs...)>> _map;
public:
    IComponent* Create(const std::string& className, Targs... args);
};
```

### 9.2 自动注册

```cpp
template<class T, typename... TArgs>
void RegistToFactory() {
    ComponentFactory<TArgs...>::GetInstance()->Regist(
        typeid(T).name(),
        [](TArgs... args) -> IComponent* {
            return DynamicObjectPool<T>::GetInstance()->MallocObject(args...);
        }
    );
}
```

### 9.3 CreateComponentC —— 协议驱动远程创建

通过 Protobuf 协议从主线程向工作线程发送创建指令：

```cpp
class CreateComponentC : public Entity<CreateComponentC>,
                         public IMessageSystem,
                         public IAwakeFromPoolSystem<> {
    void HandleCreateComponent(Packet* pPacket);  // 接收创建指令
    void HandleRemoveComponent(Packet* pPacket);  // 接收移除指令
};
```

内部使用模板递归 `DynamicCall<N>` 逐个解析变长参数类型（int/string），再调用 ComponentFactory 完成创建。

—

## 十、全局辅助

### 10.1 Singleton 模板

```cpp
template<typename T>
class Singleton {
    static T* Instance(Args&&...);    // 创建
    static T* GetInstance();          // 获取
    static void DestroyInstance();    // 销毁
};
```

### 10.2 Global —— 全局状态

```cpp
class Global : public Singleton<Global> {
    uint64 GenerateSN();              // 全局唯一 ID：(TimeTick<<32) + (serverId<<16) + ticket
    timeutil::Time TimeTick;          // 毫秒时间戳
    bool IsStop{ false };             // 全局停止标志
};
```

### 10.3 SnObject —— 唯一标识

所有实体、组件、线程均继承 SnObject，构造时自动获取全局唯一 SN，池化复用时 `ResetSN()` 重新分配。

—

## 十一、关键设计总结

### Actor 模型

本服务器的并发模型本质上是 **Actor 模式**——每个 Entity（及其附属的 Component）构成一个 Actor：

```
┌─────────────────────────────────────────────┐
│               Actor = Entity                 │
│  ┌─────────────────────────────────────┐    │
│  │  私有状态（成员变量）                    │    │
│  ├─────────────────────────────────────┤    │
│  │  Component A  │  Component B  │ ... │    │
│  ├─────────────────────────────────────┤    │
│  │  消息信箱（IMessageSystem 回调表）     │    │
│  └─────────────────────────────────────┘    │
│                                              │
│  行为：                                       │
│   • 处理消息 → 修改自身状态                     │
│   • 发送消息 → 通过 Packet 通信给其他 Actor    │
│   • 创建子 Actor → AddComponent              │
└─────────────────────────────────────────────┘
```

**Actor 三要素在本框架中的映射**：

| Actor 特征 | 本框架实现 | 说明 |
|————|————|——|
| 私有状态 | Entity/Component 的成员变量 | 外部不直接访问，只能通过消息驱动修改 |
| 消息信箱 | `_messageSystems` + MsgId 回调表 | 收到 Packet 后按 MsgId 路由到对应处理函数 |
| 无共享通信 | 双缓存 `CacheSwap` + Packet 序列化 | Actor 间不共享内存，通过消息副本传递数据 |
| 创建子 Actor | `IEntity::AddComponent<T>()` | 实体可挂载子组件，形成 Actor 树 |
| 位置透明 | 跨线程消息投递（`DispatchPacket` / `AddPacketToList`） | 发送者不关心目标 Actor 在哪个线程 |

**为什么这是 Actor 而不是纯 ECS**：

- 纯 ECS（如 Unity DOTS）强调”数据与行为分离”——System 遍历所有同类 Component 批量处理
- 本框架中，每个 Entity 自带行为（通过 IUpdateSystem/IMessageSystem），状态和逻辑绑定在一起
- 因此更准确的说法是：**用 ECS 的组合模式实现了 Actor 模型**

**线程与 Actor 的关系**：

```
Thread (子服务器) = Actor 容器
  │
  ├── Entity A (Actor)
  │     ├── Component A1
  │     └── Component A2
  │
  ├── Entity B (Actor)
  │     └── Component B1
  │
  └── 每帧：
        1. UpdateMessage() → 从信箱取消息，路由到各 Actor
        2. Update() → 各 Actor 执行帧逻辑
```

同一线程内的 Actor 共享一个事件循环（单线程无锁），不同线程的 Actor 通过消息队列异步通信——这正是经典的 Actor 调度策略。

### 并发模型

| 层级 | 机制 | 特点 |
|——|——|——|
| Actor 间（跨线程） | 消息队列 + 双缓存 | 无共享状态，Packet 通信 |
| Actor 间（同线程） | 单线程顺序调度 | 无锁，Update 按序执行 |
| IO 层 | epoll(Linux) / select(Win) | 非阻塞 Reactor |

### 性能优化

| 技术 | 效果 |
|——|——|
| 双缓存（CacheSwap） | 锁仅保护写入瞬间，读取完全无锁 |
| 对象池 | 避免高频 new/delete，减少 GC 压力 |
| epoll EPOLLET | 边沿触发，减少系统调用次数 |
| 环形缓冲 | 减少内存拷贝和扩容频率 |
| Round-Robin 分配 | 组件均衡分布到各工作线程 |

### 优雅退出

```
SIGINT → Global::IsStop = true
  → 各 Thread 循环退出
  → ThreadMgr 确认全部停止 → join 所有线程
  → Dispose 回收主线程对象
  → ObjectPoolMgr 最后回收一次 → 销毁所有池
  → 销毁单例
```

### 扩展指南

| 需求 | 做法 |
|——|——|
| 新增服务 | 继承 `ServerApp`，`InitApp` 中 `CreateComponent<T>()` |
| 新增业务组件 | 继承 `Component<T>` + 所需 System 接口 |
| 新增实体 | 继承 `Entity<T>` + System 接口，可 `AddComponent` 挂载子组件 |
| 新增协议 | `.proto` 定义 MsgId + 消息体 → `protoc` 生成 → 注册回调 |
| 新增池化对象 | 继承 `IComponent`/`ObjectBlock`，实现 `BackToPool` |

—

## 十二、完整生命周期示例

以 `NetworkListen` 组件为例，串联所有模块：

```
┌─ 创建 ─────────────────────────────────────────────────────┐
│ LoginApp::InitApp()                                         │
│   → CreateComponent<NetworkListen>(“127.0.0.1”, 2233)       │
│   → 序列化为 Protobuf → 双缓存入队                           │
│   → 主线程 Update → Round-Robin 分发到 Thread[0]            │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─ 初始化 ───────────────────────────────────────────────────┐
│ Thread[0]::UpdateMessage()                                  │
│   → CreateComponentC 处理 MI_CreateComponent                │
│   → ComponentFactory::Create → 从对象池分配                  │
│   → AwakeFromPool(“127.0.0.1”, 2233) → bind + listen       │
│   → AddToSystem → 注册到 _updateSystems + _messageSystems   │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─ 运行 ─────────────────────────────────────────────────────┐
│ 每帧：                                                       │
│   UpdateMessage() → 处理上层发来的消息                         │
│   Update() → Epoll() → Accept() → 处理收发                  │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─ 销毁 ─────────────────────────────────────────────────────┐
│ 连接断开 → _active = false                                   │
│   → 下帧 Update 检测 → 移出系统 → BackToPool → 归还对象池    │
└─────────────────────────────────────────────────────────────┘
```

—

## 附录：模块依赖关系

```
ServerApp
  ├── ThreadMgr (主服务器 + 线程调度)
  │     └── Thread[] (子服务器)
  │           ├── Network/NetworkListen/NetworkConnector
  │           ├── 业务组件 (Account, RobotTest, Console...)
  │           └── CreateComponentC (协议动态创建)
  ├── Global (时间、SN、停止标志)
  └── DynamicObjectPoolMgr
        └── DynamicObjectPool<T> (ConnectObj, Packet, 各组件...)
              └── ObjectBlock → BackToPool → FreeObject

通信层：
  Packet + Protobuf + ComponentFactory + MessageCallBackFunction
```

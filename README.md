## 一、项目总览

代码链接：https://github.com/WR0903/cppServer

### 目录结构

```plain&#x20;text
cppServer/
├── engine.sln                  # Visual Studio 解决方案
├── make-all.sh                 # Linux 编译脚本
├── bin/                        # 输出目录
└── src/
    ├── libs/libserver/         # 核心 ECS 服务器引擎库
    │   ├── component.h/.cpp          # Component 基类（ECS 组件）
    │   ├── entity.h/.cpp             # Entity 基类（ECS 实体）
    │   ├── entity_system.h/.cpp      # EntitySystem（ECS 核心引擎）
    │   ├── component_factory.h       # 组件工厂（反射创建）
    │   ├── create_component.h/.cpp   # 远程动态创建组件
    │   ├── system.h                  # System 接口定义
    │   ├── message_system.h/.cpp     # 消息系统接口
    │   ├── thread.h/.cpp             # 工作线程（子 EntitySystem）
    │   ├── thread_mgr.h/.cpp         # 线程管理器（主 EntitySystem）
    │   ├── network.h/.cpp            # 网络实体
    │   ├── connect_obj.h/.cpp        # 连接对象实体
    │   ├── object_pool.h             # 对象池（模板）
    │   └── ...                       # 工具类（cache_swap, singleton 等）
    ├── apps/login/             # 登录服务器应用
    │   ├── account.cpp/.h            # 账号组件
    │   ├── login_obj_mgr.cpp/.h      # 玩家管理组件
    │   ├── http_request.cpp/.h       # HTTP 请求组件
    │   └── ...
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
    CreateComponent<NetworkListen>("127.0.0.1", 2233); // 1. 创建网络监听组件（ECS 方式）
    _pThreadMgr->CreateComponent<RobotTest>();          // 2. 创建业务组件
    _pThreadMgr->CreateComponent<Account>();
    _pThreadMgr->CreateComponent<Console>();
}
```



### 2.2 ServerApp 构造与运行

`ServerApp` 构造时完成全局单例初始化并启动 ECS 引擎：

```c++
// server_app.cpp:6-25
ServerApp::ServerApp(APP_TYPE appType) {
    signal(SIGINT, Signalhandler);          // 注册信号处理
    _appType = appType;
    DynamicObjectPoolMgr::Instance();        // 对象池管理器
    Global::Instance();                      // 全局状态
    ThreadMgr::Instance();                   // 主 EntitySystem + 线程管理器
    _pThreadMgr = ThreadMgr::GetInstance();
    UpdateTime();

    for (int i = 0; i < 3; i++) {            // 默认创建 3 个工作线程（每个都是 EntitySystem）
        _pThreadMgr->NewThread();
    }
    _pThreadMgr->StartAllThread();
}
```

主循环 `Run()` 负责：更新时间 → `_pThreadMgr->Update()`（ECS 引擎更新：消息分发 + Component Update）→ 对象池 `Update`（回收对象），收到停止信号后按 "停线程 → 回收线程资源 → 回收主线程资源 → 销毁对象池" 顺序优雅退出。

### 2.3 架构分层（ECS）

```plain&#x20;text
┌──────────────────────────────────────────────────────┐
│                  ServerApp (主线程)                     │
│      Run(): 时间更新 + ECS 引擎 Update + 对象池回收       │
├──────────────────────────────────────────────────────┤
│            ThreadMgr (主 EntitySystem，单例)             │
│      管理子 EntitySystem，路由消息与动态创建 Component      │
├──────────┬──────────┬──────────┬───────────────────┤
│ Thread 1 │ Thread 2 │ Thread 3 │  ...                │
│(子EntityS│(子EntityS│(子EntityS│                      │
│┌───────┐│┌───────┐│┌───────┐││  每个线程是独立的         │
││Network│││Account│││RobotT │││  EntitySystem，管理       │
││Listen │││Comp   │││est    │││  各自的 Component/Entity  │
│└───────┘│└───────┘│└───────┘││                          │
│+Entity  │+Entity  │+Entity  ││                          │
│+Component│+Component│+Component│                      │
└──────────┴──────────┴──────────┴────────────────────┘
```

## 三、ECS 核心架构

### 3.1 Entity-Component-System 三层设计

框架分为两套独立的继承体系，具体业务类通过**多重继承**同时组合二者：

```plain&#x20;text
SnObject                         ← 全局唯一 SN
  ├── IComponent                 ← 组件基类
  │     ├── Component<T>         ← 类型化组件模板
  │     │     ├── Account          (账号验证组件)
  │     │     ├── NetworkLocator   (网络定位器组件)
  │     │     ├── HttpRequest      (HTTP 请求组件)
  │     │     └── ...
  │     └── IEntity               ← 实体 = 特殊组件，可容纳子组件
  │           └── Entity<T>       ← 类型化实体模板
  │                 ├── Network          (网络实体)
  │                 ├── ConnectObj       (连接对象实体)
  │                 ├── Console          (控制台实体)
  │                 └── CreateComponentC (远程创建组件实体)
  └── EntitySystem                ← 实体系统（管理所有 Component/Entity）
        ├── Thread                ← 工作线程（子 EntitySystem）
        └── ThreadMgr             ← 主线程管理器（主 EntitySystem，Singleton）

ISystem                            ← System 接口基类（独立继承树）
  ├── IAwakeFromPoolSystem<T...>   ← 对象池唤醒回调（替代旧 Init）
  ├── IUpdateSystem                ← 每帧更新回调（替代旧 Update）
  └── IMessageSystem               ← 消息回调（替代旧 MessageList）
```

> **组合方式**：`IComponent` 和 `ISystem` 是两套**平级独立**的继承树。具体类通过多重继承同时组合两者。
> 例如 `CreateComponentC` 继承 `Entity<CreateComponentC>` + `IMessageSystem` + `IAwakeFromPoolSystem<>`，同时获得 Entity 的身份和 System 的能力。

**核心设计理念**：

- **Entity 仍是 Actor**：Entity 本质上是独立的并发单元，通过消息通信。
- **Component 扩展功能**：Entity 按需组合 Component，通过 System 接口（`IAwakeFromPoolSystem` / `IUpdateSystem` / `IMessageSystem`）声明 Init、Update、消息处理能力。
- **IEntity 继承 IComponent**：Entity 本身也是一个 Component，统一由 EntitySystem 管理，实现"组合即实体"。
- **Entity 可包含子 Component**：`IEntity::AddComponent<T>()` 可从对象池分配子组件，`GetComponent<T>()` 通过 `dynamic_cast` 查找。

### 3.2 IComponent —— 组件基类

```c++
// component.h:9-37
class IComponent : virtual public SnObject {
public:
    friend class EntitySystem;

    void SetPool(IDynamicObjectPool* pPool);
    void SetParent(IEntity* pObj);
    void SetEntitySystem(EntitySystem* pSys);

    bool IsActive() const { return _active; }
    template<class T> T* GetParent();                     // 获取所属 Entity
    EntitySystem* GetEntitySystem() const;                // 获取所属引擎
    virtual void BackToPool() = 0;                        // 归还对象池
    virtual void ComponentBackToPool();                    // 通用回收流程

protected:
    bool _active{ true };
private:
    IEntity* _parent{ nullptr };
    EntitySystem* _pEntitySystem{ nullptr };
    IDynamicObjectPool* _pPool{ nullptr };
};

template<class T>
class Component : public IComponent {
public:
    virtual const char* GetTypeName();    // typeid 运行时类型
    uint64 GetTypeHashCode();            // typeid hash
};
```

组件通过 `Component<T>` 模板实现运行时类型识别（RTTI），所有业务对象只需继承 `Component<T>`。

### 3.3 IEntity —— 实体（可容纳子组件）

```c++
// entity.h:12-53
class IEntity : public IComponent {
public:
    virtual ~IEntity();

    template<class T, typename... TArgs>
    void AddComponent(TArgs... args);          // 从对象池分配子组件

    template<class T>
    T* GetComponent();                         // 按类型查找子组件

private:
    std::map<uint64, IComponent*> _components; // 子组件映射表
};

template<class T>
class Entity : public IEntity {
    virtual const char* GetTypeName();
    uint64 GetTypeHashCode();
};
```

> 例如 `Network` 继承 `Entity<Network>`，可通过 `AddComponent<SomeComponent>()` 动态挂载子组件，实现功能的模块化组合。

### 3.4 System 接口体系

```c++
// system.h
class ISystem { };                             // 纯虚基类

template<typename... TArgs>
class IAwakeSystem : virtual public ISystem {  // 首次构造时初始化
    virtual void Awake(TArgs... args) = 0;
};

template<typename... TArgs>
class IAwakeFromPoolSystem : virtual public ISystem {  // 从对象池取出时重新初始化
    virtual void AwakeFromPool(TArgs... args) = 0;
};

class IUpdateSystem : virtual public ISystem {  // 每帧 Update
    virtual void Update() = 0;
};

// message_system.h
class IMessageSystem : virtual public ISystem { // 消息处理
    virtual void RegisterMsgFunction() = 0;
    void AttachCallBackHandler(MessageCallBackFunctionInfo* pCallback);
    bool IsFollowMsgId(Packet* packet) const;
    void ProcessPacket(Packet* packet) const;
    static void DispatchPacket(Packet* pPacket);  // 广播到所有线程
    static void SendPacket(Packet* pPacket);      // 定向网络发送
};
```

**System 接口组合示例**：

```c++
// Account 组件：同时支持池化初始化和消息处理
class Account : public Component<Account>,
                public IAwakeFromPoolSystem<>,
                public IMessageSystem { ... };

// Network 实体：同时支持池化初始化和消息处理
class Network : public Entity<Network>,
                public IAwakeFromPoolSystem<NetworkType, std::string, int>,
                public IMessageSystem { ... };

// Console 实体：支持池化和每帧 Update
class Console : public Entity<Console>,
                public IAwakeFromPoolSystem<>,
                public IUpdateSystem { ... };
```

### 3.5 EntitySystem —— ECS 核心引擎

`EntitySystem` 是每个线程的运行时容器，统一管理所有 Component/Entity 的生命周期与调度：

```c++
// entity_system.h:15-51
class EntitySystem : virtual public SnObject, public IDisposable {
public:
    void InitComponent();                         // 自动添加 CreateComponentC 等基础组件

    template<class T, typename... TArgs>
    T* AddComponent(TArgs... args);               // 从对象池创建绑定到当前系统

    template<typename... TArgs>
    IComponent* AddComponentByName(std::string className, TArgs... args); // 反射创建

    template<class T>
    T* GetComponent();                            // 按类型查找组件

    virtual void Update();                        // 每帧：消息分发 + Component Update
    void UpdateMessage();                         // 双缓存消息分发
    void AddPacketToList(Packet* pPacket);        // 写入消息双缓存
    void Dispose() override;

protected:
    void AddToSystem(IComponent* pObj);           // 注册到对应 System 列表

protected:
    std::list<IUpdateSystem*> _updateSystems;     // Update 系统列表
    std::list<IMessageSystem*> _messageSystems;   // 消息系统列表
    std::map<uint64, IComponent*> _objSystems;    // 所有组件（SN 索引）
    std::mutex _packet_lock;
    CacheSwap<Packet> _cachePackets;              // 消息双缓存
};
```

**AddToSystem 核心逻辑** —— 自动类型识别与注册：

```c++
// entity_system.cpp:65-80
void EntitySystem::AddToSystem(IComponent* pComponent) {
    pComponent->SetEntitySystem(this);
    _objSystems[pComponent->GetSN()] = pComponent;

    // dynamic_cast 自动识别：是 IUpdateSystem 则加入 _updateSystems
    const auto objUpdate = dynamic_cast<IUpdateSystem*>(pComponent);
    if (objUpdate != nullptr)
        _updateSystems.emplace_back(objUpdate);

    // 是 IMessageSystem 则注册回调并加入 _messageSystems
    const auto objMsg = dynamic_cast<IMessageSystem*>(pComponent);
    if (objMsg != nullptr) {
        objMsg->RegisterMsgFunction();
        _messageSystems.emplace_back(objMsg);
    }
}
```

**每帧 Update 流程** —— 消息分发 + 组件更新 + 自动回收：

```c++
// entity_system.cpp:13-33
void EntitySystem::Update() {
    UpdateMessage();                  // 1. 双缓存交换 → 遍历分发消息

    auto iter = _updateSystems.begin();
    while (iter != _updateSystems.end()) {
        auto pComponent = dynamic_cast<IComponent*>(*iter);
        if (!pComponent->IsActive()) {
            // 2. 非活跃组件自动归还对象池
            _objSystems.erase(pComponent->GetSN());
            iter = _updateSystems.erase(iter);
            pComponent->ComponentBackToPool();
        } else {
            (*iter)->Update();        // 3. 正常组件每帧 Update
            ++iter;
        }
    }
}
```

### 3.6 Thread —— 工作线程（子 EntitySystem）

```c++
// thread.h:15-28
class Thread : public EntitySystem {          // 直接继承 EntitySystem！
public:
    Thread();
    void Start();
    bool IsRun() const;
    bool IsStop() const;
    bool IsDispose();
private:
    ThreadState _state;
    std::thread _thread;
};
```

启动后进入 ECS 事件循环：

```c++
// thread.cpp:11-26
void Thread::Start() {
    _thread = std::thread([this]() {
        InitComponent();                       // 自动安装 CreateComponentC 等基础组件
        _state = ThreadState_Run;
        while (!Global::GetInstance()->IsStop) {
            Update();                          // EntitySystem::Update（消息+组件Update）
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        _state = ThreadState_Stoped;
    });
}
```

### 3.7 ThreadMgr —— 主线程 + 线程管理器（主 EntitySystem）

```c++
// thread_mgr.h:15-52
class ThreadMgr : public Singleton<ThreadMgr>, public EntitySystem {
public:
    template<class T, typename... TArgs>
    void CreateComponent(TArgs... args);       // 动态创建组件（序列化到子线程）
    void DispatchPacket(Packet* pPacket);      // 广播消息
    void Update() override;                    // 每帧分发创建指令 + EntitySystem::Update
private:
    std::vector<Thread*> _threads;
    size_t _threadIndex{ 0 };
    std::mutex _create_lock;
    CacheSwap<Packet> _createPackets;          // 创建组件指令双缓存
};
```

**CreateComponent —— 远程动态创建组件**：

```c++
// thread_mgr.h:54-72
template<class T, typename... TArgs>
inline void ThreadMgr::CreateComponent(TArgs... args) {
    std::lock_guard<std::mutex> guard(_create_lock);

    const std::string className = typeid(T).name();
    if (!ComponentFactory<TArgs...>::GetInstance()->IsRegisted(className))
        RegistToFactory<T, TArgs...>();       // 自动注册到工厂

    // 将创建指令序列化为 Protobuf 消息
    Proto::CreateComponent proto;
    proto.set_class_name(className.c_str());
    AnalyseParam(proto, std::forward<TArgs>(args)...);

    auto pCreatePacket = new Packet(Proto::MsgId::MI_CreateComponent, 0);
    pCreatePacket->SerializeToBuffer(proto);
    _createPackets.GetWriterCache()->emplace_back(pCreatePacket);
}
```

**主线程 Update** —— 轮询分发创建指令 + ECS Update：

```c++
// thread_mgr.cpp:17-37
void ThreadMgr::Update() {
    // 1. 交换创建指令双缓存
    _create_lock.lock();
    if (_createPackets.CanSwap()) { _createPackets.Swap(); }
    _create_lock.unlock();

    // 2. 轮询分发到子线程
    auto pList = _createPackets.GetReaderCache();
    for (auto iter = pList->begin(); iter != pList->end(); ++iter) {
        if (_threadIndex >= _threads.size()) _threadIndex = 0;
        _threads[_threadIndex]->AddPacketToList(*iter);
        _threadIndex++;
    }
    pList->clear();

    // 3. 主线程自己的 ECS Update
    EntitySystem::Update();
}
```

**DispatchPacket** —— 广播到主线程 + 所有子线程：

```c++
// thread_mgr.cpp:84-94
void ThreadMgr::DispatchPacket(Packet* pPacket) {
    AddPacketToList(pPacket);                  // 主线程
    for (auto iter = _threads.begin(); iter != _threads.end(); ++iter)
        (*iter)->AddPacketToList(pPacket);     // 所有子线程
}
```

### 3.8 ComponentFactory —— 反射创建组件

支持按**类名字符串**运行时动态创建组件：

```c++
// component_factory.h:7-62
template<typename... Targs>
class ComponentFactory {
public:
    typedef std::function<IComponent*(Targs...)> FactoryFunction;

    bool Regist(const std::string& className, FactoryFunction pFunc);
    bool IsRegisted(const std::string& className);
    IComponent* Create(const std::string className, Targs... args);

private:
    std::map<std::string, FactoryFunction> _map;   // className → 创建函数
    std::mutex _lock;
};
```

配合 `RegistToFactory<T, TArgs...>` 模板实现自动注册：

```c++
// regist_to_factory.h — 编译期自动将类型注册到 ComponentFactory
template<class T, typename... TArgs>
void RegistToFactory() {
    ComponentFactory<TArgs...>::GetInstance()->Regist(
        typeid(T).name(),
        [](TArgs... args) -> IComponent* {
            auto pObj = DynamicObjectPool<T>::GetInstance()->MallocObject(args...);
            return pObj;
        }
    );
}
```

### 3.9 CreateComponentC —— 协议驱动的远程动态创建

通过 Protobuf 协议从网络接收创建指令，在工作线程中动态实例化 Component：

```c++
// create_component.h:7-18
class CreateComponentC : public Entity<CreateComponentC>,
                         public IMessageSystem,
                         public IAwakeFromPoolSystem<> {
    void HandleCreateComponent(Packet* pPacket) const;   // 处理创建指令
    void HandleRemoveComponent(Packet* pPacket);         // 处理移除指令
};
```

核心实现 —— 编译期递归解析变长参数：

```c++
// create_component.cpp:14-52
template<size_t ICount>
struct DynamicCall {
    template<typename... TArgs>
    static IComponent* Invoke(EntitySystem* pEntitySystem, const std::string classname,
                               std::tuple<TArgs...> t1,
                               google::protobuf::RepeatedPtrField<Proto::CreateComponentParam>& params) {
        if (params.size() == 0)
            return ComponentFactoryEx(pEntitySystem, classname, t1, ...);

        Proto::CreateComponentParam param = (*(params.begin()));
        params.erase(params.begin());

        if (param.type() == Proto::CreateComponentParam::Int)
            return DynamicCall<ICount-1>::Invoke(pEntitySystem, classname,
                        std::tuple_cat(t1, std::make_tuple(param.int_param())), params);
        if (param.type() == Proto::CreateComponentParam::String)
            return DynamicCall<ICount-1>::Invoke(pEntitySystem, classname,
                        std::tuple_cat(t1, std::make_tuple(param.string_param())), params);
        return nullptr;
    }
};
```

**创建流程**：`ThreadMgr::CreateComponent<T>()` → 序列化为 Proto → 通过双缓存分发到工作线程 → `CreateComponentC::HandleCreateComponent` → `DynamicCall` 解析参数 → `ComponentFactory::Create` → `EntitySystem::AddToSystem`。

```plain&#x20;text
ThreadMgr::CreateComponent<Account>(args...)
    │
    ├─ 1. 自动注册到 ComponentFactory（首次）
    ├─ 2. 序列化为 Proto::CreateComponent
    └─ 3. 写入 _createPackets 双缓存
         │
         ▼ (下一帧 ThreadMgr::Update 轮询分发)
    Thread[N]::AddPacketToList(MI_CreateComponent)
         │
         ▼ (子线程 UpdateMessage)
    CreateComponentC::HandleCreateComponent
         │
         ├─ 4. DynamicCall<N> 解析变长参数
         ├─ 5. ComponentFactory::Create(className, args...)
         └─ 6. EntitySystem::AddToSystem(pComponent)
              ├─ dynamic_cast → IUpdateSystem → _updateSystems
              └─ dynamic_cast → IMessageSystem → _messageSystems
```

## 四、游戏循环与运行时流程

### 4.1 整体运行时架构

```plain&#x20;text
ServerApp::Run()                                   主线程循环
  │
  ├─ UpdateTime()                                  更新时间戳
  ├─ ThreadMgr::Update()
  │   ├─ ① 处理 _createPackets                     组件创建消息（双缓存交换）
  │   ├─ ② 轮询分发到 _threads[]                     均衡分配
  │   └─ ③ EntitySystem::Update()                  主线程自己的 ECS 更新
  └─ DynamicObjectPoolMgr::Update()                 对象池回收

 同时每个工作线程在独立 std::thread 中运行自己的循环（见 4.3）。
```

### 4.2 ThreadMgr —— 线程管理器

`ThreadMgr` 是单例，同时继承 `EntitySystem`：既是线程调度中心，也是主线程自己的 ECS 容器。内部维护一个 `Thread*` 数组和轮询索引，实现创建请求的均匀分配。

**创建组件流程**：`CreateComponent<T>(args...)` 不直接创建组件，而是将类型名和参数序列化为 Protobuf 消息，存入双缓存队列。主线程 `Update()` 时交换缓存，**轮询（round-robin）** 分发到各工作线程，由目标线程的 `CreateComponentC` 反序列化后调用 `ComponentFactory::Create()` 完成实际创建。

**消息广播**：`DispatchPacket` 同时向主线程和所有子线程投递消息包。

**生命周期**：`CreateThread` → `StartAllThread` → 运行中检查 `IsStopAll` / `IsDisposeAll` → `Dispose` 销毁全部。

### 4.3 Thread —— 工作线程

每个 `Thread` 继承 `EntitySystem`，封装一个 `std::thread`。启动流程：

1. `InitComponent()` — 创建 `CreateComponentC` 实体，用于处理远程组件创建消息
2. 进入游戏循环 — 每帧调用 `Update()`（消息处理 → 组件更新），间隔 1ms 让出 CPU
3. 收到全局停止信号后退出循环，状态标记为 `Stoped`

状态机：`Init → Run → Stoped`。主线程通过 `IsStop()` 检查状态，`IsDispose()` 中 `join()` 等待线程结束并回收。

### 4.4 AddToSystem —— 组件注册

这是 ECS 运行时的核心入口。任何 Component/Entity 创建后都会经过此方法：

```plain&#x20;text
AddToSystem(pComponent)
  │
  ├─ pComponent->SetEntitySystem(this)            绑定所属引擎
  ├─ _objSystems[SN] = pComponent                 全局对象映射
  │
  ├─ dynamic_cast<IUpdateSystem*>(pComponent)
  │   └─ 非空 → _updateSystems.push_back()         注册为可更新组件
  │
  └─ dynamic_cast<IMessageSystem*>(pComponent)
      └─ 非空 → RegisterMsgFunction()              注册消息回调（子类实现）
              → _messageSystems.push_back()        注册为消息处理者
```

三种 System 接口的注册时机：

| System 接口 | 注册时机 | 触发方式 |
|-------------|----------|----------|
| `IMessageSystem` | `AddToSystem` 中自动 | `dynamic_cast` + `RegisterMsgFunction()` |
| `IUpdateSystem` | `AddToSystem` 中自动 | `dynamic_cast` |
| `IAwakeFromPoolSystem` | 对象池分配后 | 调用方手动执行 `AwakeFromPool(args...)` |

### 4.5 EntitySystem::Update —— 每帧更新

每帧严格执行 **先消息、后更新** 的顺序：

```plain&#x20;text
EntitySystem::Update()
  │
  ├─ ① UpdateMessage()
  │   ├─ 交换 _cachePackets（双缓存，swap 时加锁）     消息入队线程安全
  │   ├─ 遍历 _messageSystems[]
  │   │   └─ IsFollowMsgId → ProcessPacket()           按 MsgId 匹配分发
  │   └─ 清空已处理包
  │
  └─ ② 遍历 _updateSystems[]
      ├─ IsActive() == true  → Update()                正常更新
      └─ IsActive() == false → 从 _objSystems 移除
                               从 _updateSystems 移除
                               ComponentBackToPool()    归还对象池
```

**关键细节**：

- **消息优先**：先处理本帧收到的所有消息，再执行 Component 的 Update。消息处理结果能在同一帧 Update 中体现。
- **自动回收**：Component 设置 `_active = false` 后，下一帧 Update 自动检测并从系统中移除、归还对象池，无需手动管理生命周期。
- **双缓存无锁读**：消息入队（`AddPacketToList`）加锁写入 writer-cache，`UpdateMessage` 只处理 reader-cache，swap 时短暂加锁。

### 4.6 完整生命周期示例

以 `Network`（网络监听实体）的完整流程串联以上各环节：

```plain&#x20;text
┌── 1. 创建 ──────────────────────────────────────────────────┐
│ ThreadMgr::CreateComponent<NetworkListen>("127.0.0.1", 2233) │
│   → Protobuf 序列化 → _createPackets writer-cache 入队        │
│                                                              │
│ ThreadMgr::Update()（主线程）                                  │
│   → _createPackets.Swap() → 轮询分发到 Thread[0]              │
│   → Thread[0].AddPacketToList(packet)                        │
└──────────────────────────────────────────────────────────────┘
                            ↓
┌── 2. 初始化 ────────────────────────────────────────────────┐
│ Thread[0]::Update()                                         │
│   → UpdateMessage()                                         │
│     → CreateComponentC::HandleCreateComponent(packet)       │
│       → Protobuf 反序列化 className + params                │
│       → ComponentFactory::Create("NetworkListen", ...)      │
│       → AddToSystem(pComponent)                             │
│         ├─ dynamic_cast<IUpdateSystem*>  ✓ → _updateSystems │
│         └─ dynamic_cast<IMessageSystem*> ✓ → _messageSystems│
│                  + RegisterMsgFunction()                    │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌── 3. 运行 ──────────────────────────────────────────────────┐
│ 每帧：UpdateMessage() 处理收包  →  Update() 执行业务逻辑       │
│ Entity 通过 IMessageSystem::SendPacket() 发送消息            │
│ Entity 通过 IMessageSystem::DispatchPacket() 广播消息        │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌── 4. 销毁 ──────────────────────────────────────────────────┐
│ 如连接断开 → _active = false                                 │
│   → 下一帧 Update() 检测到 !IsActive()                       │
│   → 从 _objSystems / _updateSystems / _messageSystems 移除  │
│   → ComponentBackToPool() 归还对象池                         │
└─────────────────────────────────────────────────────────────┘
```

---

## 五、网络 Epoll / Reactor 模块

### 5.1 类继承体系（ECS 架构下）

```plain&#x20;text
IComponent
  └─ IEntity
       └─ Entity<T>
            └─ Network (network.h)              ← 网络实体基类，IO 多路复用
                 ├─ NetworkListen               ← 服务端监听 + Accept
                 └─ NetworkConnector            ← 客户端连接 + 断线重连

ConnectObj (connect_obj.h)                     ← 连接实体（池化，继承 Entity<ConnectObj>）
  ├─ RecvNetworkBuffer                         ← 接收环形缓冲
  └─ SendNetworkBuffer                         ← 发送环形缓冲
```



### 5.2 平台 IO 抽象

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

### 5.3 Epoll 实现（Reactor 核心）

#### 5.3.1 初始化与事件注册

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

#### 5.3.2 Epoll 事件循环（Reactor dispatch）

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

#### 5.3.3 EPOLLET 边沿触发

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

### 5.4 NetworkListen —— 服务端监听

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

### 5.5 NetworkConnector —— 客户端连接

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

### 5.6 ConnectObj —— 连接对象（池化）

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

### 5.7 Socket 选项配置

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

## 六、Protobuf 通信模块

### 6.1 Proto 定义

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

### 6.2 Packet —— 协议封装

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

### 6.3 网络数据包格式

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

### 6.4 消息回调系统（MessageList / IMessageSystem）

`MessageList` 提供消息注册与分发，是消息处理的基础：

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

## 七、ECS 消息流转

### 7.1 消息机制概述

Entity 是消息处理的主体（Actor），`IMessageSystem` 作为 Component 实现消息回调。`EntitySystem::AddToSystem` 通过 `dynamic_cast<IMessageSystem*>` 自动识别并注册，统一调度。

### 7.2 IMessageSystem —— 消息处理者接口

```c++
// message_system.h
class IMessageSystem : virtual public ISystem {
public:
    virtual void RegisterMsgFunction() = 0;
    void AttachCallBackHandler(MessageCallBackFunctionInfo* pCallback);
    bool IsFollowMsgId(Packet* packet) const;
    void ProcessPacket(Packet* packet) const;
    static void DispatchPacket(Packet* pPacket);   // 广播到所有线程
    static void SendPacket(Packet* pPacket);       // 定向网络发送
};
```

EntitySystem 在 `AddToSystem` 时通过 `dynamic_cast<IMessageSystem*>` 自动识别并加入 `_messageSystems` 列表。



### 7.3 消息收发流程

```plain&#x20;text
                  ┌─────────────────────────────────┐
                  │        收消息路径                 │
                  └─────────────────────────────────┘
   ConnectObj::Recv
        │
        ▼
   RecvNetworkBuffer::GetPacket  (解析出 Packet)
        │
        ├─ 广播: ThreadMgr::DispatchPacket → 主+所有子 EntitySystem._cachePackets(写)
        └─ 定向: EntitySystem::AddPacketToList → 单 EntitySystem._cachePackets(写)
        │
        ▼ (下一帧)
   EntitySystem::UpdateMessage → CacheSwap::Swap → 遍历 _messageSystems
        │
        ▼
   IMessageSystem::ProcessPacket → MessageCallBackFunction → 注册的回调

                  ┌─────────────────────────────────┐
                  │        发消息路径                 │
                  └─────────────────────────────────┘
   IMessageSystem::SendPacket(pPacket)
        │
        ▼
   NetworkListen::SendPacket → _sendMsgList(写)
        │
        ▼ (下一帧)
   Network::Update → CacheSwap::Swap → ConnectObj::SendPacket → 发送缓冲 → epoll 发送

                  ┌─────────────────────────────────────────┐
                  │  动态创建 Component 协议路径                │
                  └─────────────────────────────────────────┘
   ThreadMgr::CreateComponent<T>(args...)    ← 主线程调用
        │
        ├─ 自动注册到 ComponentFactory
        ├─ 序列化为 Proto::CreateComponent
        └─ 写入 _createPackets(写)
        │
        ▼ (下一帧 ThreadMgr::Update 轮询)
   分发到 Thread[N]._cachePackets(写)
        │
        ▼ (子线程 UpdateMessage)
   CreateComponentC::HandleCreateComponent   ← 处理 MI_CreateComponent 协议
        │
        ├─ DynamicCall<N> 解析变长参数
        ├─ ComponentFactory::Create(className, args...)
        └─ EntitySystem::AddToSystem → 自动注册 Update/Message System
```

### 7.4 双缓存实现无锁读

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

### 7.5 线程间通信示例

业务组件（如 `Account`）发送消息：

```c++
// 任何 IMessageSystem 中均可调用
IMessageSystem::SendPacket(pPacket);
// → NetworkListen::SendPacket → 网络
```

网络收到消息后广播给所有业务 Component：

```c++
// connect_obj.cpp:125-132
if (_pNetWork->IsBroadcast())
    ThreadMgr::GetInstance()->DispatchPacket(pPacket);  // 广播到所有 EntitySystem
```

## 八、内存池模块

### 8.1 整体设计

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

### 8.2 ObjectBlock —— 池化对象基类

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


### 8.3 DynamicObjectPool —— 模板对象池


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

#### 8.3.1 分配对象

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

#### 8.3.2 回收对象

对象 `Dispose()` → `BackToPool()` → `FreeObject`：

```c++
// object_pool.h:163-168
void DynamicObjectPool<T>::FreeObject(ObjectBlock* pObj) {
    std::lock_guard<std::mutex> guard(_inUseLock);
    _objInUse.GetRemoveCache()->emplace_back(dynamic_cast<T*>(pObj));  // 标记移除
}
```

#### 8.3.3 定期合并

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

### 8.4 ConnectObj 池化示例

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

### 8.5 缓冲区管理（Buffer）

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

环形缓冲通过 `_dataSize` 记录有效数据量，解决"首尾重合"歧义：

```c++
// network_buffer.h:43-46
class NetworkBuffer : public Buffer {
protected:
    unsigned int _dataSize;          // 有效数据量
};
```

扩容策略：每次增加 `ADDITIONAL_SIZE`(128KB)，上限 `MAX_SIZE`(1MB)。

### 8.6 调试与监控

通过 `ConsoleCmdPool` 控制台命令查看对象池状态：

```c++
// console_cmd_pool.cpp:16-20
void ConsoleCmdPool::HandleShow(std::vector<std::string>& params) {
    DynamicObjectPool<ConnectObj>::GetInstance()->Show();  // 输出 free/inUse/totalCall
}
```

`Show()` 在 Debug 模式输出总数量、空闲数、使用数、累计分配次数。

## 九、全局辅助模块


### 9.1 Singleton —— 单例模板


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


### 9.2 Global —— 全局状态与 SN 生成


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

### 9.3 SnObject —— 全局唯一标识

```c++
// sn_object.h:6-29
class SnObject {
    SnObject() { _sn = Global::GetInstance()->GenerateSN(); }
    void ResetSN() { _sn = Global::GetInstance()->GenerateSN(); }  // 池化复用时重置
    uint64 GetSN() const { return _sn; }
};
```

`Thread`、`EntitySystem`、`IComponent`、`IEntity`、`ObjectBlock` 均继承此类，便于对象追踪与日志。

### 9.4 IDisposable —— 资源释放接口

```c++
// disposable.h:3-11
class IDisposable {
    virtual void Dispose() = 0;
};
```

所有需要资源管理的类统一实现此接口，由容器在销毁时统一调用。

## 十、关键设计总结

### 10.1 并发模型

| 层级  | 机制                        | 说明                      |
|------|----------------------------|--------------------------|
| 线程间 | 消息队列 + 双缓存              | 无共享状态，靠 Packet 通信     |
| 线程内 | 单线程事件循环                 | Update 驱动，无锁处理消息      |
| IO   | epoll(Linux)/select(Win)   | 非阻塞 IO + Reactor         |

### 10.2 性能优化点

1. **双缓存无锁读**：`CacheSwap`/`CacheRefresh` 使锁仅保护瞬间写入，处理阶段无锁。

2. **对象池**：`ConnectObj` 等高频对象池化，避免频繁 new/delete。

3. **epoll 边沿触发**：`EPOLLET` 减少系统调用次数。

4. **环形缓冲**：收发缓冲环形设计，减少内存拷贝与扩容。

5. **轮询负载均衡**：`ThreadMgr` 轮询分配对象到线程，均衡负载。

### 10.3 优雅退出流程

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

### 10.4 扩展性

* **新增应用**：继承 `ServerApp`，实现 `InitApp` 通过 `CreateComponent<T>()` 添加网络与业务组件。

* **新增业务 Component**：继承 `Component<T>` + 对应 System 接口（`IAwakeFromPoolSystem`、`IUpdateSystem`、`IMessageSystem`），通过 `ThreadMgr::CreateComponent<T>()` 加入 ECS 引擎。

* **新增 Entity**：继承 `Entity<T>` + 对应 System 接口，可通过 `AddComponent<T>()` 动态挂载子组件。

* **新增协议**：在 `.proto` 中定义消息与 MsgId，`protoc` 生成代码后注册回调。

* **新增池化对象**：继承 `IComponent` 或 `ObjectBlock`，实现 `BackToPool`，用 `DynamicObjectPool<T>` 管理。

* **协议动态创建 Component**：客户端发送 `MI_CreateComponent` 协议 → `CreateComponentC` 处理 → `ComponentFactory` 反射创建 → 自动加入 EntitySystem。

## 十一、模块依赖关系图（ECS 架构）

```plain&#x20;text
                         ┌──────────┐
                         │ ServerApp│
                         └────┬─────┘
                 ┌─────────────┼────────────┐
                 ▼             ▼            ▼
          ┌──────────┐  ┌──────────┐  ┌────────────┐
          │ ThreadMgr│  │  Global  │  │  PoolMgr   │
          │(主EntityS│  └──────────┘  └─────┬──────┘
          │  ystem ) │                       │
          └────┬─────┘              ┌────────┴───────┐
               │                    │ ObjectPool<T>  │
          ┌────┴────┐               │ (Component/    │
          │ Thread  │               │  Entity/Packet)│
          │(子Entity│               └────────┬───────┘
          │ System) │                        │
          │  (多个) │               ┌────────┴───────┐
          └────┬────┘               │  IComponent    │
               │                    │  IEntity       │
         ┌─────┴──────┐             │  ConnectObj... │
         │EntitySystem│             └────────────────┘
         │ (消息+组件) │
         └─────┬──────┘
               │
  ┌────────────┼──────────────┐
  ▼            ▼              ▼
┌───────┐ ┌─────────┐ ┌────────────┐
│Net-   │ │业务组件  │ │CreateComp- │
│Listen │ │Account  │ │onentC      │
│Entity │ │Component│ │Entity      │
└───┬───┘ │RobotTest│ │(协议动态    │
    │     │Component│ │ 创建组件)   │
    ▼     └─────────┘ └────────────┘
  epoll/select
    │
    ▼
┌──────────────────────┐
│  Packet + Protobuf   │
│  + ComponentFactory  │
│  (反射 + 动态创建)     │
└──────────────────────┘
```




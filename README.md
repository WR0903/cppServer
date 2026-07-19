# C++ 多进程多线程网络服务器

> 源码地址：[https://github.com/WR0903/cppServer](https://github.com/WR0903/cppServer)

基于 ECS 架构 + Actor 模型的 C++ 游戏服务器框架，支持多进程分布式部署与多线程并发处理，使用 epoll/select IO 多路复用。

---

## 目录结构

```
cppServer/
├── src/
│   ├── libs/libserver/          # 核心服务器库（ECS + 网络 + 内存池 + 线程管理）
│   ├── apps/
│   │   ├── allinone/            # 全合一进程（所有服务合并部署）
│   │   ├── login/               # 登录服务器进程
│   │   └── dbmgr/              # 数据库管理进程
│   └── tools/robots/            # 压测机器人
├── engine.sln                   # Visual Studio 解决方案
└── make-all.sh                  # Linux 编译脚本
```

---

## 多进程架构

本框架采用**多进程分布式架构**，每个服务可独立部署为一个进程，也可通过 `allinone` 模式合并为单进程运行。

### 进程类型（APP_TYPE）

| 进程类型 | 枚举值 | 职责 |
|---------|--------|------|
| `APP_LOGIN` | 登录服务器 | 处理客户端登录验证 |
| `APP_DB_MGR` | 数据库管理器 | 数据库读写操作 |
| `APP_GAME` | 游戏服务器 | 游戏逻辑处理 |
| `APP_SPACE` | 场景服务器 | 场景/地图管理 |
| `APP_GAME_MGR` | 游戏管理器 | 游戏服务调度 |
| `APP_SPACE_MGR` | 场景管理器 | 场景服务调度 |
| `APP_ALLINONE` | 全合一 | 所有服务合并为单进程 |
| `APP_ROBOT` | 压测机器人 | 模拟客户端压力测试 |

### 部署模式

```
┌─ 分布式部署（多进程） ──────────────────────────────────┐
│                                                         │
│  [Login进程] ←──TCP──→ [DBMgr进程]                      │
│       ↕                      ↕                          │
│  [Game进程] ←──TCP──→ [Space进程]                       │
│                                                         │
└─────────────────────────────────────────────────────────┘

┌─ 单机部署（全合一） ────────────────────────────────────┐
│                                                         │
│  [allinone 进程]                                        │
│    ├── Login 组件                                       │
│    ├── DBMgr 组件                                       │
│    ├── Game 组件                                        │
│    └── Space 组件                                       │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

### 进程间通信

进程间通过 **TCP 网络连接** 通信：
- `NetworkListen`：服务端监听，接受其他进程连接
- `NetworkConnector`：客户端连接，主动连接目标进程（支持断线重连）

示例：Login 进程连接 DBMgr 进程：
```cpp
// login/main.cpp
pThreadMgr->CreateComponent<NetworkListen>(ListenThread, ip, port);        // 监听客户端
pThreadMgr->CreateComponent<NetworkConnector>(ConnectThread, APP_DB_MGR, 0); // 连接DBMgr
```

---

## 多线程架构

每个进程内部采用**多线程并发模型**，按职责将线程分为不同类型。

### 线程类型（ThreadType）

| 线程类型 | 职责 | 说明 |
|---------|------|------|
| `MainThread` | 主线程 | 全局调度、组件创建分发、对象池回收 |
| `ListenThread` | 监听线程 | 运行 NetworkListen，处理 accept 和 IO 事件 |
| `ConnectThread` | 连接线程 | 运行 NetworkConnector，管理对外连接 |
| `LogicThread` | 逻辑线程 | 业务组件运行（Account、RobotTest 等） |
| `MysqlThread` | 数据库线程 | MySQL 异步读写操作 |

### 线程模型图

```
┌─────────────────────────────────────────────────────────────┐
│  MainThread（主线程）                                         │
│    ├── 全局调度 ThreadMgr::Update()                          │
│    ├── 组件创建请求分发（Round-Robin）                         │
│    └── 对象池回收 DynamicPacketPool::Update()                │
├─────────────────────────────────────────────────────────────┤
│  工作线程（按 ThreadType 分组，每组可有多个线程实例）              │
│  ┌──────────────┬──────────────┬────────────┬─────────────┐ │
│  │ ListenThread │ ConnectThread│ LogicThread│ MysqlThread │ │
│  │ NetworkListen│ NetworkConn  │ Account    │ MysqlConn   │ │
│  │ Accept+IO    │ 对外连接+IO   │ 业务逻辑    │ DB读写      │ │
│  └──────────────┴──────────────┴────────────┴─────────────┘ │
├─────────────────────────────────────────────────────────────┤
│  线程间通信：消息队列 + 双缓存（CacheSwap），锁仅保护写入瞬间     │
└─────────────────────────────────────────────────────────────┘
```

### 线程间通信机制

- **双缓存（CacheSwap）**：写端和读端各持一个 list，swap 时只交换指针，读取阶段完全无锁
- **消息广播（DispatchPacket）**：同时投递到主线程 + 所有子线程
- **定向投递（AddPacketToList）**：投递到指定线程的消息队列

---

## ECS 核心架构（Entity = Actor）

本框架中，**每个 Entity 就是一个 Actor**——拥有私有状态、消息信箱和独立行为，Actor 间通过消息通信，不共享内存。

### 继承体系

```
身份与数据                          行为能力
──────────────                     ──────────────
SnObject                           ISystem
  └── IComponent                     ├── IAwakeFromPoolSystem<T...>  （池化初始化）
        ├── Component<T>             ├── IUpdateSystem               （每帧更新）
        └── IEntity                  └── IMessageSystem              （消息回调）
              └── Entity<T>
```

### Entity 即 Actor

```
┌─────────────────────────────────────────────┐
│            Actor = Entity<T>                 │
│  ┌─────────────────────────────────────┐    │
│  │  私有状态（成员变量）                    │    │
│  ├─────────────────────────────────────┤    │
│  │  子组件: Component A │ Component B  │    │
│  ├─────────────────────────────────────┤    │
│  │  消息信箱（IMessageSystem 回调表）     │    │
│  └─────────────────────────────────────┘    │
│                                              │
│  行为：                                       │
│   • 处理消息 → 修改自身状态                     │
│   • 发送消息 → 通过 Packet 与其他 Actor 通信   │
│   • 创建子 Actor → AddComponent              │
└─────────────────────────────────────────────┘
```

**Actor 三要素映射**：

| Actor 特征 | 框架实现 | 说明 |
|-----------|---------|------|
| 私有状态 | Entity/Component 成员变量 | 外部只能通过消息驱动修改 |
| 消息信箱 | `IMessageSystem` + MsgId 回调表 | 按 MsgId 路由到处理函数 |
| 无共享通信 | 双缓存 CacheSwap + Packet 序列化 | Actor 间不共享内存 |
| 创建子 Actor | `IEntity::AddComponent<T>()` | 实体可挂载子组件形成 Actor 树 |
| 位置透明 | 跨线程消息投递 | 发送者不关心目标 Actor 在哪个线程 |

> **Thread = Actor 容器**：同一线程内的 Actor 共享事件循环（单线程无锁），不同线程的 Actor 通过消息队列异步通信。

### 组合示例

```cpp
// 业务组件 = 身份 + 行为组合
class Account : public Component<Account>,
                public IAwakeFromPoolSystem<>,
                public IMessageSystem { ... };

class NetworkListen : public Entity<NetworkListen>,
                      public IAwakeFromPoolSystem<std::string, int>,
                      public IUpdateSystem,
                      public IMessageSystem { ... };
```

### EntitySystem 调度

每个线程拥有一个 EntitySystem，每帧执行：
1. **UpdateMessage()** → 双缓存交换 → 按 MsgId 路由到对应 Actor
2. **Update()** → 遍历所有 IUpdateSystem 组件 → 自动回收失活 Actor

---

## 网络模块

### IO 模型

- Linux：epoll（边沿触发）
- Windows：select

### 核心类

| 类 | 职责 |
|----|------|
| `Network` | 网络基类，封装 epoll/select 事件循环 |
| `NetworkListen` | 服务端监听 + Accept |
| `NetworkConnector` | 客户端连接 + 断线重连 |
| `ConnectObj` | 池化连接对象，含环形收发缓冲 |

### 数据包格式

```
┌──────────────┬──────────────┬──────────────────────┐
│ TotalSize    │ MsgId        │ Protobuf Payload     │
│ (2 bytes)    │ (2 bytes)    │ (变长)               │
└──────────────┴──────────────┴──────────────────────┘
```

---

## 内存管理

### 对象池

```
DynamicObjectPoolMgr（单例）
  └── DynamicObjectPool<T>
        ├── _free: queue<T*>           空闲对象队列
        └── _objInUse: CacheRefresh<T>  使用中（双缓存延迟回收）
```

- **分配**：空闲队列取出 → ResetSN → AwakeFromPool 初始化
- **回收**：_active=false → 下帧检测 → BackToPool → 延迟归还空闲队列

### 环形缓冲区

收发数据使用环形缓冲，避免频繁内存拷贝，扩容策略：每次 +128KB，上限 1MB。

---

## 启动流程

```cpp
int main(int argc, char* argv[]) {
    ServerApp app(curAppType, argc, argv);
    app.Initialize();       // 信号注册 + 全局单例 + 线程初始化

    // 创建业务组件（按线程类型分配）
    pThreadMgr->CreateThread(ListenThread, 1);
    pThreadMgr->CreateComponent<NetworkListen>(ListenThread, ip, port);

    app.Run();              // 主循环
    app.Dispose();          // 优雅退出
}
```

### 主循环

```
Run() → while(!IsStop) {
    UpdateTime()                    // 更新时间戳
    ThreadMgr::Update()            // 分发创建请求 + 分发消息
    DynamicPacketPool::Update()    // 对象池回收
    sleep(1ms)
}
→ 等待所有线程停止 → 销毁线程 → 释放资源
```

---

## 关键设计总结

| 设计 | 实现方式 |
|------|---------|
| 多进程通信 | TCP 网络连接（NetworkListen / NetworkConnector） |
| 多线程通信 | 消息队列 + 双缓存（CacheSwap），近乎无锁 |
| IO 模型 | epoll Reactor（Linux）/ select（Windows） |
| 内存管理 | 模板对象池 + 双缓存延迟回收 |
| 组件创建 | 反射工厂 + Protobuf 协议驱动跨线程创建 |
| 并发模型 | Actor 模式（ECS 组合实现），同线程无锁顺序调度 |
| 优雅退出 | SIGINT → 全局停止标志 → 等待线程退出 → join → 资源释放 |

---

## 扩展指南

| 需求 | 做法 |
|------|------|
| 新增服务进程 | 定义 APP_TYPE → 新建 main.cpp → 初始化对应组件 |
| 新增业务组件 | 继承 `Component<T>` + 所需 System 接口 |
| 新增网络实体 | 继承 `Entity<T>` + System 接口，可挂载子组件 |
| 新增协议 | `.proto` 定义 MsgId + 消息体 → protoc 生成 → 注册回调 |
| 新增线程类型 | 在 ThreadType 枚举中添加 → CreateThread 指定数量 |
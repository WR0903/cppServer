# C++ 多进程多线程网络游戏服务器

> 源码地址：[https://github.com/WR0903/cppServer](https://github.com/WR0903/cppServer)

基于 ECS + Actor 模型的 C++ 游戏服务器框架，支持多进程分布式部署与多线程并发，使用 epoll/select IO 多路复用。

---

## 编译指南

### 系统要求

- 操作系统：Linux（Ubuntu 20.04+ / Debian 11+）

### 编译器要求

- C++ 标准：C++14
- GCC 版本：5.0+（推荐 GCC 9+ 或 GCC 13）
- 支持 `-pthread`、`-Wall`、`-DEPOLL` 编译选项

### CMake 要求

- CMake 最低版本：3.5（protobuf 3.21.12 要求）
- 推荐 CMake 版本：3.16+

安装 CMake：

```bash
# Ubuntu/Debian
sudo apt install cmake

# 或安装最新版本（推荐）
sudo apt install cmake pip
pip install cmake --upgrade
```

### 系统库安装

以下库无法源码编译，需通过系统包管理器安装：

```bash
# 一条命令安装全部系统库
sudo apt install build-essential cmake libmysqlclient-dev libssl-dev uuid-dev protobuf-compiler

# 分项说明：
# build-essential      → GCC、G++、make 等基础编译工具
# cmake                → CMake 构建系统
# libmysqlclient-dev   → MySQL 客户端开发库（mysqlclient）
# libssl-dev           → OpenSSL 开发库（ssl、crypto）
# uuid-dev             → UUID 开发库（libuuid）
# protobuf-compiler    → protoc 编译器（用于编译 .proto 文件）
```

**验证安装**：

```bash
# 检查编译器
g++ --version

# 检查 CMake
cmake --version

# 检查 protoc 版本（应为 3.21.x）
protoc --version

# 检查系统库
dpkg -l | grep -E "libmysqlclient-dev|libssl-dev|uuid-dev"
```

### 编译方法

```bash
# Debug 编译（默认）
./make-all.sh

# Release 编译
./make-all.sh release

# 清理编译缓存
./make-all.sh clean

# 手动编译
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j4

# Release 手动编译
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j4
```

编译完成后，可执行文件输出到 `bin/` 目录。

---

## 运行指南

前置条件：安装 Python3 + PyYAML（`pip3 install pyyaml`），确保 MySQL、redis 服务已启动。

```bash
cd bin
./start.sh       # 启动（自动识别 Debug/Release，按依赖顺序启动：appmgr → dbmgr → login → game → space）
./stop.sh        # 停止（按依赖逆序优雅退出：space → game → login → dbmgr → appmgr）
./allinoned      # 一体化模式（单进程运行所有服务，Debug）
./allinone       # 一体化模式（Release）
```

### 运行效果

Unity 客户端登录并进入游戏：

![Unity 登录效果](game.png)

### 编译产物

| Debug 模式 | Release 模式 | 说明 |
|---|---|---|
| appmgrd | appmgr | 应用管理器 |
| logind | login | 登录服务 |
| dbmgrd | dbmgr | 数据库管理 |
| gamed | game | 游戏逻辑 |
| spaced | space | 场景服务 |
| allinoned | allinone | 一体化服务 |
| robotsd | robots | 测试机器人 |

### 清理

所有编译缓存（`.o`、`.d`、CMake 缓存等）均在 `build/` 目录下：

```bash
rm -rf build
```

或使用：

```bash
./make-all.sh clean
```

---

## 目录结构

```
cppServer/
├── src/
│   ├── libs/
│   │   ├── libserver/       # 核心库（ECS + 网络 + 内存池 + 线程管理）
│   │   ├── libplayer/       # 玩家公共库
│   │   └── libresource/     # 资源管理库（CSV 配置加载）
│   ├── apps/
│   │   ├── allinone/        # 全合一进程
│   │   ├── appmgr/          # 应用管理器（GameMgr + SpaceMgr）
│   │   ├── login/           # 登录服务器
│   │   ├── game/            # 游戏服务器
│   │   ├── space/           # 场景服务器
│   │   └── dbmgr/           # 数据库管理
│   └── tools/robots/        # 压测机器人
├── res/engine.yaml           # 服务器配置
└── make-all.sh              # 编译入口
```

---

## 多进程架构

| 进程类型 | 枚举值 | 职责 |
|---------|--------|------|
| `APP_DB_MGR` | `1` | MySQL/Redis 读写 |
| `APP_GAME_MGR` | `1<<1` | 游戏服务调度 |
| `APP_SPACE_MGR` | `1<<2` | 场景服务调度 |
| `APP_LOGIN` | `1<<3` | 客户端登录验证 + HTTP 接口 |
| `APP_GAME` | `1<<4` | 游戏逻辑（大厅、世界代理） |
| `APP_SPACE` | `1<<5` | 场景/地图管理、移动系统 |
| `APP_APPMGR` | `GAME_MGR\|SPACE_MGR` | 应用管理器（世界创建与进程同步） |
| `APP_ALLINONE` | 全部按位或 | 单进程运行所有服务 |
| `APP_ROBOT` | `1<<6` | 压力测试 |

> `GetAppKey() = (appType << 32) + appId` 生成全局唯一进程标识。

### 部署模式

```
分布式：[AppMgr] ←TCP→ [Login] ←TCP→ [DBMgr]
              ↕              ↕
        [Game]  ←TCP→ [Space]

全合一：[allinone 进程] 内含所有服务组件，无需网络通信
```

### 进程间通信

通过 TCP/HTTP 网络连接，**NetworkLocator** 统一管理连接注册与查找：

| NetworkType | 说明 |
|-------------|------|
| `TcpListen` / `TcpConnector` | TCP 服务端/客户端 |
| `HttpListen` / `HttpConnector` | HTTP 服务端/客户端 |

---

## 多线程架构

线程数量由 `engine.yaml` 配置，支持单线程模式（`thread_logic=0` + `thread_mysql=0`）。

### 线程类型

| 类型 | 职责 |
|------|------|
| `MainThread` | 全局调度、组件创建分发、对象池回收 |
| `ListenThread` | NetworkListen，处理 accept + IO |
| `ConnectThread` | NetworkConnector，对外连接 + 断线重连 |
| `LogicThread` | 业务组件运行 |
| `MysqlThread` | MySQL 异步读写（独占式分发） |

### 线程管理层次

```
ThreadMgr（主线程，Singleton + SystemManager）
  ├── 全局组件：Yaml, Log4, NetworkLocator, Console
  ├── ThreadCollector（Round-Robin）→ Listen / Connect / Logic 线程
  └── ThreadCollectorExclusive（独占式）→ Mysql 线程
```

**每个 Thread 继承 SystemManager**，拥有独立的 EntitySystem + MessageSystem + UpdateSystem + 对象池。

### 线程间通信

| 机制 | 说明 |
|------|------|
| 双缓存（CacheSwap） | 写/读各持一个 list，swap 只交换指针，读取无锁 |
| 消息广播 | 投递到主线程 + 所有子线程 |
| Packet 引用计数 | 跨线程广播时 `AddRef/RemoveRef`，全部处理完才回收 |

### 配置示例

```yaml
allinone:
  thread_logic: 2
  thread_mysql: 2
  thread_listen: 2
  thread_connector: 1
  ip: 114.132.58.120
  port: 5401
  http_port: 7071
```

---

## ECS 核心架构（Entity = Actor）

**每个 Entity 就是一个 Actor**——私有状态 + 消息信箱 + 独立行为，Actor 间通过消息通信，不共享内存。

### 继承体系

```
SnObject → IComponent → Component<T> / IEntity → Entity<T>

System 体系：System → ISystem<T> → MessageSystem / UpdateSystem / MoveSystem

初始化接口：IAwakeSystem<Args...>（单例）/ IAwakeFromPoolSystem<Args...>（池化）
```

### Actor 映射

| Actor 特征 | 框架实现 |
|-----------|---------|
| 私有状态 | Entity/Component 成员变量 |
| 消息信箱 | MessageSystem + MsgId → IMessageCallBack 回调表 |
| 无共享通信 | 双缓存 CacheSwap + Packet 引用计数 |
| 位置透明 | 跨线程投递 + NetworkLocator |

> **Thread = Actor 容器**：同线程内无锁顺序调度，跨线程通过消息队列异步通信。

### 组件类型

| 类型 | `IsSingle()` | 对象池预分配 | 场景 |
|------|-------------|------------|------|
| 单例组件 | true | 1 个 | Yaml、Console 等全局唯一 |
| 池化组件 | false | 50 个 | Packet、ConnectObj 等高频创建 |

### SystemManager 每帧调度

```
SystemManager::Update()
  ├── PoolCollector::Update()      → 对象池延迟回收
  ├── EntitySystem::Update()       → ComponentCollections::Swap()
  ├── MessageSystem::Update()      → 双缓存交换 → 路由消息到 Actor
  └── UpdateSystem / MoveSystem    → 帧更新 / 自定义逻辑
```

`ComponentCollections` 延迟添加/删除：Add/Remove 先暂存，Swap 时统一生效，避免遍历时修改容器。

---

## 网络模块

- **IO 模型**：Linux epoll（边沿触发）/ Windows select
- **数据包格式**：`[TotalSize 2B][MsgId 2B][Protobuf Payload]`
- **S2S 扩展头**：`[MsgId 2B][EntitySn 8B][PlayerSn 8B][Payload]`
- **消息回调**：普通回调 `MsgCallbackFun` / Filter 回调 `MessageCallBackFilter<T>`（先定位目标对象再处理）

| 核心类 | 职责 |
|--------|------|
| `Network` | 网络基类，封装 epoll/select 事件循环 |
| `NetworkListen` | 服务端监听 + Accept |
| `NetworkConnector` | 客户端连接 + 断线重连 |
| `ConnectObj` | 池化连接对象，含环形收发缓冲区 |
| `NetworkLocator` | 统一管理网络实例注册与查找 |

---

## 内存管理

```
DynamicObjectPoolCollector（每线程独立）
  └── DynamicObjectPool<T>
        ├── _free: queue<T*>          空闲队列
        └── _objInUse: CacheRefresh   使用中（双缓存延迟回收）
```

- **分配**：空闲队列取出 → SetSN → Awake 初始化
- **回收**：BackToPool → 下帧 Swap 归还空闲队列
- **Packet 池**：全局单例 `DynamicPacketPool`，支持引用计数跨线程安全回收

---

## 启动流程

```
main() → ServerApp::Initialize()
  → 解析参数(-sid) → 注册信号 → 创建 Global/PacketPool/ThreadMgr
  → InitializeComponentXxx() → 创建网络组件
  → Run() 主循环：UpdateTime → ThreadMgr::Update → PacketPool::Update → sleep(1ms)
  → 优雅退出：等待线程停止 → join → 释放资源
```

---

## 扩展指南

| 需求 | 做法 |
|------|------|
| 新增进程 | `APP_TYPE` 添加位值 → 新建 main.cpp + InitializeComponentXxx |
| 新增组件 | 继承 `Entity<T>` + `IAwakeFromPoolSystem<>` → CreateComponent 注册 |
| 新增 System | 继承 `ISystem<T>` → 实现 Update → CreateSystem 注册 |
| 新增协议 | .proto 定义 → protoc 生成 → RegisterFunction 注册回调 |
| 单线程模式 | engine.yaml 中 thread_logic/thread_mysql 设为 0 |

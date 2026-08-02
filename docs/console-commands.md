# Console 指令手册

> 服务器运行时在终端直接输入指令，格式：`<模块> <子命令> [参数]`

---

## 通用指令（所有进程可用）

| 指令 | 说明 |
|------|------|
| `help` | 列出当前进程已注册的所有模块及子命令 |
| `-exit` | 优雅退出当前进程 |

---

## 框架模块（所有 Server 进程通用）

### `app` — 应用信息

| 指令 | 说明 |
|------|------|
| `app -info` | 显示当前进程的应用信息（进程类型、启动参数等） |
| `app -help` | 显示帮助 |

### `thread` — 线程状态

| 指令 | 说明 |
|------|------|
| `thread -entity` | 显示各线程中的 Entity 列表 |
| `thread -pool` | 显示各线程的对象池使用情况（含 DynamicPacketPool） |
| `thread -connect` | 显示各线程的网络连接信息 |
| `thread -help` | 显示帮助 |

### `efficiency` — 性能统计

| 指令 | 说明 |
|------|------|
| `efficiency -thread` | 显示各线程耗时/效率统计 |
| `efficiency -help` | 显示帮助 |

### `trace` — 链路追踪

| 指令 | 说明 |
|------|------|
| `trace -connect <socket>` | 查看指定 socket 的连接追踪信息 |
| `trace -packet <socket>` | 查看指定 socket 相关的所有收发包记录 |
| `trace -player <socket>` | 查看指定 socket 对应玩家的追踪记录 |
| `trace -account <account>` | 按账号名查看追踪记录 |
| `trace -time` | 查看时间相关的追踪信息 |
| `trace -clean` | 清空所有追踪数据 |
| `trace -help` | 显示帮助 |

---

## Robot 进程专属模块

> 以下指令仅在 `robotsd` / `robots` 进程中可用。

### `login` — 机器人登录

| 指令 | 示例 | 说明 |
|------|------|------|
| `login -a <account>` | `login -a test001` | 单个账号登录（主线程创建，方便后续手动操作） |
| `login -ex <prefix> <count>` | `login -ex bot 100` | 批量登录：以 prefix 为前缀，创建 count 个机器人（bot0 ~ bot99），自动按线程数均匀分配 |
| `login -help` | | 显示帮助 |

### `http` — HTTP 接口测试

| 指令 | 示例 | 说明 |
|------|------|------|
| `http -check <account> <password>` | `http -check test 123456` | 通过 HTTP 接口验证账号密码 |
| `http -help` | | 显示帮助 |

### `world` — 世界/场景操作

| 指令 | 示例 | 说明 |
|------|------|------|
| `world -enter <world_id>` | `world -enter 1` | 让当前机器人进入指定世界（需先 `login -a` 登录） |
| `world -help` | | 显示帮助 |

---

## 使用示例

### 服务器进程（allinoned / gamed / spaced 等）

```bash
# 查看所有可用指令
help

# 查看进程信息
app -info

# 查看所有线程的 Entity
thread -entity

# 查看连接情况
thread -connect

# 查看线程性能
efficiency -thread

# 追踪某个账号的操作链路
trace -account player001

# 追踪某个 socket 的收发包
trace -packet 15

# 清空追踪数据
trace -clean

# 退出服务器
-exit
```

### Robot 压测进程（robotsd / robots）

```bash
# 单个登录（用于手动测试流程）
login -a test001

# 登录后进入世界
world -enter 1

# 批量创建 100 个机器人压测
login -ex bot 100

# HTTP 账号验证测试
http -check myaccount mypassword

# 查看机器人连接状态
thread -connect

# 查看线程负载
efficiency -thread

# 退出
-exit
```

---

## 扩展指南

新增 Console 指令只需 3 步：

### 1. 创建 ConsoleCmd 子类

```cpp
// console_cmd_xxx.h
class ConsoleCmdXxx : public ConsoleCmd
{
public:
    void RegisterHandler() override;
    void HandleHelp() override;

private:
    void HandleFoo(std::vector<std::string>& params);
};
```

### 2. 注册子命令

```cpp
// console_cmd_xxx.cpp
void ConsoleCmdXxx::RegisterHandler()
{
    OnRegisterHandler("-foo", BindFunP1(this, &ConsoleCmdXxx::HandleFoo));
}

void ConsoleCmdXxx::HandleHelp()
{
    std::cout << "\t-foo <param>.\t\tdescription" << std::endl;
}

void ConsoleCmdXxx::HandleFoo(std::vector<std::string>& params)
{
    if (!CheckParamCnt(params, 1))
        return;
    // 处理逻辑...
}
```

### 3. 在进程 main.cpp 中注册模块

```cpp
auto pConsole = pThreadMgr->GetEntitySystem()->GetComponent<Console>();
pConsole->Register<ConsoleCmdXxx>("xxx");  // "xxx" 就是输入时的模块名
```

之后在终端输入 `xxx -foo param1` 即可触发。

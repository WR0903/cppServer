# 角色数据存盘机制

本文档梳理角色（Player）数据在数据库中的存储结构、存盘时机、读写链路，以及如何扩展新的存盘字段。

---

## 1. 存盘时机

角色存盘在 **Space 进程** 中触发，时机为**玩家断线**：

```
玩家断线 → World::HandleNetworkDisconnect
  → Player::SerializeToProto(protoSave.mutable_player())   // 收集内存数据
  → SendPacket(G2DB_SavePlayer, protoSave, APP_DB_MGR)     // 发给 DBMgr
```

> 目前仅在断线时存盘，没有定时存盘机制。

---

## 2. 存盘调用链

```
Space::World::HandleNetworkDisconnect
  │
  ├─ Player::SerializeToProto(Proto::Player* pProto)
  │    │
  │    ├─ pProto->CopyFrom(_player)           // 拷贝内存中的 Player proto 基础数据
  │    │
  │    └─ 遍历所有 Component
  │         └─ dynamic_cast<PlayerComponent*>  // 只调用实现了 PlayerComponent 接口的组件
  │              └─ pPlayerComponent->SerializeToProto(pProto)   // 各组件写回自己的字段
  │
  └─ SendPacket(G2DB_SavePlayer)  →  DBMgr
                                        │
                                        └─ MysqlConnector::OnSavePlayer
                                             ├─ protoPlayer.base().SerializeToString()  → blob
                                             ├─ protoPlayer.misc().SerializeToString()  → blob
                                             └─ ExecuteStmt("update player set base=?, misc=?, savetime=now() where sn=?")
```

---

## 3. MySQL 表结构

`player` 表在 DBMgr 启动时自动创建（`mysql_table_update.cpp:127`）：

```sql
CREATE TABLE `player` (
    `sn`         bigint(20)  NOT NULL,      -- 角色唯一ID
    `name`       char(32)    NOT NULL,      -- 角色名
    `account`    char(64)    NOT NULL,      -- 账号名
    `base`       blob,                      -- PlayerBase 序列化
    `item`       blob,                      -- 预留（当前未使用）
    `misc`       blob,                      -- PlayerMisc 序列化
    `savetime`   datetime    default NULL, -- 存盘时间
    `createtime` datetime    default NULL, -- 创建时间
    PRIMARY KEY (`sn`),
    UNIQUE KEY `NAME` (`name`),
    KEY `ACCOUNT` (`account`)
) ENGINE=InnoDB;
```

---

## 4. 哪些数据会存盘

### MySQL 列与 Proto 字段映射

| MySQL 列 | Proto 结构 | 字段内容 | 序列化方式 |
|----------|-----------|---------|-----------|
| `sn` | `Player.sn` | 角色 SN（uint64） | 直接值 |
| `name` | `Player.name` | 角色名 | 直接值 |
| `account` | — | 账号名（创建时写入，存盘不更新） | 直接值 |
| `base` | `PlayerBase` | `gender`（性别）、`level`（等级） | protobuf → blob |
| `misc` | `PlayerMisc` | `last_world`（上次公共地图）、`last_dungeon`（上次副本）、`online_version`（乐观锁版本号） | protobuf → blob |
| `item` | — | 预留字段，当前未使用 | — |
| `savetime` | — | 存盘时间（MySQL `now()` 自动） | MySQL 自动 |
| `createtime` | — | 创建时间（MySQL `now()` 自动） | MySQL 自动 |

### Proto 数据结构定义（db.proto）

```protobuf
message Vector3 {
    float x = 1;
    float y = 2;
    float z = 3;
}

message PlayerBase {
    Gender gender = 1;
    int32 level = 2;
}

message LastWorld {
    int32 world_id = 1;
    int64 world_sn = 2;
    Vector3 position = 3;
}

message PlayerMisc {
    LastWorld last_world = 1;      // 上次公共地图
    LastWorld last_dungeon = 2;    // 上次副本地图
    int32 online_version = 3;      // 在线版本（乐观锁）
}

message Player {
    uint64 sn = 1;
    string name = 2;
    PlayerBase base = 3;
    PlayerMisc misc = 4;
}
```

### LastWorld 详解

`last_world` / `last_dungeon` 记录玩家上次所在位置，用于下次登录时回到原地图：

| 字段 | 说明 |
|------|------|
| `world_id` | 地图配置 ID（对应 `res/resource/world.csv`） |
| `world_sn` | 地图实例 SN（运行时生成的唯一实例标识） |
| `position` | 玩家在地图中的坐标（Vector3: x, y, z） |

---

## 5. 存盘 SQL

```sql
-- 创建角色（仅一次，mysql_connector.cpp:130）
INSERT INTO player (sn, account, name, savetime, createtime)
VALUES (?, ?, ?, now(), now())

-- 存盘（每次断线触发，mysql_connector.cpp:133）
UPDATE player SET base=?, misc=?, savetime=now() WHERE sn=?
```

> **注意**：存盘只更新 `base` 和 `misc` 两列。`name`、`account` 创建后不更新，`item` 列预留未用。

---

## 6. Component 参与存盘的机制

### PlayerComponent 接口

```cpp
// libplayer/player_component.h
class PlayerComponent
{
public:
    virtual void ParserFromProto(const Proto::Player& proto) = 0;   // 从存档加载
    virtual void SerializeToProto(Proto::Player* pProto) = 0;       // 写入存档
};
```

### 自动调用逻辑

`Player::SerializeToProto` 遍历所有 Component，只要继承了 `PlayerComponent` 接口，就会被自动调用：

```cpp
// player.cpp:93
void Player::SerializeToProto(Proto::Player* pProto) const
{
    pProto->CopyFrom(_player);        // 基础数据

    for (auto pair : _components)     // 遍历所有组件
    {
        auto pPlayerComponent = dynamic_cast<PlayerComponent*>(pair.second);
        if (pPlayerComponent == nullptr)
            continue;
        pPlayerComponent->SerializeToProto(pProto);  // 组件写回自己的字段
    }
}
```

读盘同理，`Player::ParserFromProto` 会调用所有 `PlayerComponent` 的 `ParserFromProto`。

### 已实现存盘的 Component

| Component | 所在进程 | 存盘字段 |
|-----------|---------|---------|
| `PlayerComponentLastMap` | 共享库（libplayer） | `misc.last_world`、`misc.last_dungeon` |
| `PlayerComponentDetail` | Space | `base.gender`（只读，SerializeToProto 空实现） |

---

## 7. 读盘（加载）流程

```
DBMgr（MySQL）
  │
  │  G2DB_QueryPlayerRs（Proto::Player 完整序列化）
  ↓
Game::Lobby::HandleQueryPlayerRs
  │  ParserFromProto → 各 Component 从 proto 加载数据
  │  进入地图流程（Teleport → Space）
  ↓
Space::World::HandleSyncPlayer
  │  ParserFromProto → 各 Component 从 proto 加载数据
  │  运行中各 Component 修改内存数据
  ↓
玩家断线
  │  SerializeToProto → 各 Component 写回 Proto::Player
  │  G2DB_SavePlayer（Proto::Player）
  ↓
DBMgr::OnSavePlayer
  │  base.SerializeToString → blob
  │  misc.SerializeToString → blob
  ↓
MySQL: UPDATE player SET base=?, misc=? WHERE sn=?
```

### 查询 SQL

```sql
-- 查询角色列表（选择角色界面）
SELECT sn, name, base, item, misc FROM player WHERE account = 'xxx'

-- 查询单个角色（登录游戏后加载完整数据）
SELECT sn, name, account, base, item, misc FROM player WHERE sn = xxx
```

---

## 8. 数据流转全貌

```
                  ┌──────────────┐
                  │   MySQL DB   │
                  │  player 表   │
                  └──────┬───────┘
                         │
            ┌────────────┼────────────┐
            ↓            │            ↑
      G2DB_QueryPlayerRs │     G2DB_SavePlayer
            │            │            │
            ↓            │            │
    ┌────────────┐       │     ┌────────────┐
    │   Game     │       │     │   Space    │
    │  (Lobby)   │       │     │  (World)   │
    │            │       │     │            │
    │ParserFrom  │       │     │SerializeTo │
    │Proto(加载) │       │     │Proto(存盘) │
    └─────┬──────┘       │     └────────────┘
          │              │
    Teleport(中转)       │
          │              │
    ┌─────↓──────┐       │
    │   Space    │       │
    │  (World)   │       │
    │            │       │
    │ParserFrom  │       │
    │Proto(加载) │       │
    └────────────┘       │
          │              │
    ┌─────↓──────┐  │
    │  Component │  │
    │  运行时修改  │  │
    │  内存数据   │  │
    └────────────┘  │
```

---

## 9. 如何扩展新的存盘字段

以背包系统为例：

### 1. 在 db.proto 中定义数据

```protobuf
message BagItem {
    uint64 item_id = 1;
    int32 count = 2;
}

message Bag {
    int32 capacity = 1;
    repeated BagItem items = 2;
}

// 在 Player 中新增字段
message Player {
    // ... 已有字段 ...
    Bag bag = 5;       // 新增
}
```

### 2. 创建 Component 继承 PlayerComponent

```cpp
class PlayerComponentBag : public Component<PlayerComponentBag>,
                          public IAwakeFromPoolSystem<>,
                          public PlayerComponent    // ← 关键：继承此接口才会被存盘调用
{
    void ParserFromProto(const Proto::Player& proto) override;
    void SerializeToProto(Proto::Player* pProto) override;
};
```

### 3. 挂载到 Player

在 `World::HandleSyncPlayer` 中：
```cpp
pPlayer->AddComponent<PlayerComponentBag>();
```

### 4. 修改 DBMgr 存盘逻辑

在 `mysql_msg.cpp` 的 `OnSavePlayer` 中，新增 blob 列的写入：

```cpp
// 新增：背包数据存盘
std::string bagStr;
protoPlayer.bag().SerializeToString(&bagStr);
AddParamBlob(stmtSave, (void*)bagStr.c_str(), (int)bagStr.size());
```

同时修改 SQL 语句和表结构，新增 `bag` 列。

> 详细步骤参考 [背包系统开发文档](bag-component.md)。

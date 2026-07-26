# 添加背包系统

以背包系统为例，演示如何在本工程中为 Player 添加一个完整的 Component，涵盖数据定义、组件实现、消息协议、挂载注册的完整流程。

---

## 整体思路

```
Player（Entity）
  ├── PlayerComponentDetail       // 已有：性别等基础信息
  ├── PlayerComponentLastMap      // 已有：上次地图位置
  └── PlayerComponentBag         ← 新增：背包组件
        ├── Awake()              → 从 Player 的 Proto 存档加载数据
        ├── 业务逻辑              → AddItem / RemoveItem / GetItemCount
        └── SerializeToProto()   → 写回 Player 的 Proto 存档（自动入库）
```

背包数据跟随玩家存档，所以放在 `Proto::Player` 中，由 DBMgr 自动存取。

---

## 第一步：定义 proto 数据结构

### 1. 修改 `src/libs/libserver/protobuf/db.proto`

在文件末尾添加背包相关结构，并将 `Bag` 嵌入 `Player`：

```protobuf
message BagItem {
    uint64 item_id = 1;     // 物品ID
    int32 count = 2;         // 数量
    int32 slot = 3;          // 格子位置
}

message Bag {
    int32 capacity = 1;           // 背包容量
    repeated BagItem items = 2;  // 物品列表
}
```

修改 `Player` 消息，新增 `bag` 字段：

```protobuf
message Player {
    uint64 sn = 1;
    string name = 2;
    PlayerBase base = 3;
    PlayerMisc misc = 4;
    Bag bag = 5;             // 新增：背包
}
```

> **要点**：`db.proto` 中的结构会存入数据库，修改后需重新执行 `protoc` 编译（项目 CMake 会自动完成）。

### 2. 修改 `src/libs/libserver/protobuf/proto_id.proto`

添加背包相关消息 ID：

```protobuf
// bag
C2S_BagSync      = 1507;   // 客户端请求同步背包
S2C_BagSync      = 1508;   // 服务端推送背包数据
C2S_ItemUse      = 1509;   // 客户端使用物品
```

### 3. 修改 `src/libs/libserver/protobuf/msg.proto`

添加网络消息体：

```protobuf
message BagSync {
    repeated BagItem items = 1;
    int32 capacity = 2;
}

message ItemUse {
    uint64 item_id = 1;
    int32 count = 2;
}
```

> **消息 ID 范围**：`C2S_` 前缀（1500+）表示客户端发给 Space，通过 Game 的 WorldProxy 中转到达 Space 的 World 组件。

---

## 第二步：创建 Component 文件

在 `src/apps/space/` 下新建两个文件。

### `player_component_bag.h`

```cpp
#pragma once
#include "libserver/component.h"
#include "libserver/system.h"
#include "libplayer/player_component.h"

#include <map>

class PlayerComponentBag : public Component<PlayerComponentBag>,
                          public IAwakeFromPoolSystem<>,
                          public PlayerComponent
{
public:
    void Awake() override;
    void BackToPool() override;

    // 存档读写（继承自 PlayerComponent）
    void ParserFromProto(const Proto::Player& proto) override;
    void SerializeToProto(Proto::Player* pProto) override;

    // 业务逻辑
    bool AddItem(uint64 itemId, int count);
    bool RemoveItem(uint64 itemId, int count);
    int GetItemCount(uint64 itemId) const;
    const std::map<uint64, Proto::BagItem>& GetItems() const;

private:
    std::map<uint64, Proto::BagItem> _items;  // itemId -> item
    int _capacity{ 50 };
};
```

### `player_component_bag.cpp`

```cpp
#include "player_component_bag.h"
#include "libplayer/player.h"

void PlayerComponentBag::Awake()
{
    Player* pPlayer = dynamic_cast<Player*>(_parent);
    ParserFromProto(pPlayer->GetPlayerProto());
}

void PlayerComponentBag::BackToPool()
{
    _items.clear();
    _capacity = 50;
}

void PlayerComponentBag::ParserFromProto(const Proto::Player& proto)
{
    _items.clear();
    const auto& bag = proto.bag();
    _capacity = bag.capacity();
    for (int i = 0; i < bag.items_size(); i++)
    {
        const auto& item = bag.items(i);
        _items[item.item_id()] = item;
    }
}

void PlayerComponentBag::SerializeToProto(Proto::Player* pProto)
{
    auto bag = pProto->mutable_bag();
    bag->set_capacity(_capacity);
    for (const auto& pair : _items)
    {
        auto item = bag->add_items();
        item->CopyFrom(pair.second);
    }
}

bool PlayerComponentBag::AddItem(uint64 itemId, int count)
{
    auto iter = _items.find(itemId);
    if (iter != _items.end())
    {
        iter->second.set_count(iter->second.count() + count);
        return true;
    }
    if (static_cast<int>(_items.size()) >= _capacity)
        return false;  // 背包已满

    Proto::BagItem item;
    item.set_item_id(itemId);
    item.set_count(count);
    item.set_slot(static_cast<int32>(_items.size()));
    _items[itemId] = item;
    return true;
}

bool PlayerComponentBag::RemoveItem(uint64 itemId, int count)
{
    auto iter = _items.find(itemId);
    if (iter == _items.end() || iter->second.count() < count)
        return false;
    iter->second.set_count(iter->second.count() - count);
    if (iter->second.count() == 0)
        _items.erase(iter);
    return true;
}

int PlayerComponentBag::GetItemCount(uint64 itemId) const
{
    auto iter = _items.find(itemId);
    return iter != _items.end() ? iter->second.count() : 0;
}

const std::map<uint64, Proto::BagItem>& PlayerComponentBag::GetItems() const
{
    return _items;
}
```

---

## 第三步：挂载到 Player

### 修改 `src/apps/space/world.cpp`

在 `HandleSyncPlayer` 函数中，玩家进入地图时挂载背包组件：

```cpp
void World::HandleSyncPlayer(Packet* pPacket)
{
    // ... 已有代码 ...

    pPlayer->ParserFromProto(playerSn, proto.player());
    pPlayer->AddComponent<PlayerComponentDetail>();

    // 新增：挂载背包组件
    pPlayer->AddComponent<PlayerComponentBag>();

    const auto pComponentLastMap = pPlayer->AddComponent<PlayerComponentLastMap>();
    // ... 后续已有代码 ...
}
```

> **要点**：`AddComponent` 会从对象池取出组件并调用 `Awake()`，`Awake` 中通过 `_parent` 拿到 Player 并从 Proto 数据加载背包。

---

## 第四步：注册消息处理

### 修改 `src/apps/space/world.h`

添加消息处理函数声明：

```cpp
void HandleBagSync(Player* pPlayer, Packet* pPacket);
void HandleItemUse(Player* pPlayer, Packet* pPacket);
```

### 修改 `src/apps/space/world.cpp`

在 `World::Awake` 中注册消息回调：

```cpp
void World::Awake(int worldId)
{
    // ... 已有代码 ...

    pMsgSystem->RegisterFunctionFilter<Player>(this, Proto::MsgId::C2S_Move,
        BindFunP1(this, &World::GetPlayer), BindFunP2(this, &World::HandleMove));

    // 新增：背包消息
    pMsgSystem->RegisterFunctionFilter<Player>(this, Proto::MsgId::C2S_BagSync,
        BindFunP1(this, &World::GetPlayer), BindFunP2(this, &World::HandleBagSync));
    pMsgSystem->RegisterFunctionFilter<Player>(this, Proto::MsgId::C2S_ItemUse,
        BindFunP1(this, &World::GetPlayer), BindFunP2(this, &World::HandleItemUse));
}
```

实现处理函数：

```cpp
void World::HandleBagSync(Player* pPlayer, Packet* pPacket)
{
    auto pBag = pPlayer->GetComponent<PlayerComponentBag>();
    if (pBag == nullptr)
        return;

    Proto::BagSync proto;
    proto.set_capacity(pBag->GetCapacity());
    for (const auto& pair : pBag->GetItems())
    {
        auto item = proto.add_items();
        item->CopyFrom(pair.second);
    }
    MessageSystemHelp::SendPacket(Proto::MsgId::S2C_BagSync, proto, pPlayer);
}

void World::HandleItemUse(Player* pPlayer, Packet* pPacket)
{
    auto proto = pPacket->ParseToProto<Proto::ItemUse>();
    auto pBag = pPlayer->GetComponent<PlayerComponentBag>();
    if (pBag == nullptr)
        return;

    if (pBag->RemoveItem(proto.item_id(), proto.count()))
    {
        // 使用物品效果...
        // 同步背包给客户端
        // ...
    }
}
```

> **消息流转路径**：客户端发送 `C2S_BagSync` → Game 的 WorldProxy 默认中转 → Space 的 World::HandleBagSync → 回复 `S2C_BagSync` 给客户端。

---

## 第五步：存档自动持久化

背包数据存在 `Proto::Player` 中，无需额外处理存档。当玩家断线时，`World::HandleNetworkDisconnect` 已经会将 Player 数据存入数据库：

```cpp
// world.cpp 已有代码，无需修改
void World::HandleNetworkDisconnect(Packet* pPacket)
{
    // ...
    Proto::SavePlayer protoSave;
    protoSave.set_player_sn(pPlayer->GetPlayerSN());
    pPlayer->SerializeToProto(protoSave.mutable_player());  // ← 自动调用各组件的 SerializeToProto
    MessageSystemHelp::SendPacket(Proto::MsgId::G2DB_SavePlayer, protoSave, APP_DB_MGR);
    // ...
}
```

`SerializeToProto` 会调用所有实现了 `PlayerComponent` 接口的组件的 `SerializeToProto`，背包数据自动写入 `Player.bag` 字段并存入数据库。

---

## 文件清单

| 文件 | 操作 |
|------|------|
| `src/libs/libserver/protobuf/db.proto` | 新增 `BagItem`、`Bag` 消息，`Player` 加 `bag` 字段 |
| `src/libs/libserver/protobuf/proto_id.proto` | 新增 `C2S_BagSync`、`S2C_BagSync`、`C2S_ItemUse` 消息 ID |
| `src/libs/libserver/protobuf/msg.proto` | 新增 `BagSync`、`ItemUse` 消息体 |
| `src/apps/space/player_component_bag.h` | **新建**，背包组件声明 |
| `src/apps/space/player_component_bag.cpp` | **新建**，背包组件实现 |
| `src/apps/space/world.h` | 添加 `HandleBagSync`、`HandleItemUse` 声明 |
| `src/apps/space/world.cpp` | `Awake` 注册消息，`HandleSyncPlayer` 挂载组件，实现消息处理函数 |

---

## 核心模式总结

| 步骤 | 做什么 | 参考的已有代码 |
|------|--------|---------------|
| 定义数据 | `db.proto` 加结构，嵌入 `Player` | `PlayerBase`、`PlayerMisc` |
| 定义协议 | `proto_id.proto` 加 ID，`msg.proto` 加消息体 | `C2S_Move`、`S2C_EnterWorld` |
| 创建组件 | 继承 `Component<T>` + `IAwakeFromPoolSystem<>` + `PlayerComponent` | `PlayerComponentDetail` |
| 挂载组件 | `HandleSyncPlayer` 中 `pPlayer->AddComponent<T>()` | `AddComponent<PlayerComponentDetail>()` |
| 注册消息 | `World::Awake` 中 `RegisterFunctionFilter` | `C2S_Move` 的注册 |
| 存档 | 实现 `ParserFromProto` / `SerializeToProto`，无需额外代码 | `PlayerComponentDetail` |

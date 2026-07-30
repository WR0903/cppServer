# AOI 九宫格系统

## 概述

AOI（Area Of Interest，兴趣区域管理）负责解决"某个玩家需要看到哪些其他玩家"的问题。本框架采用**九宫格**方案，将世界坐标划分为固定大小的格子，每个玩家只关心自己所在格子及相邻 8 个格子内的实体。

### 核心参数

| 参数 | 值 | 说明 |
|------|---|------|
| 格子大小 | 50.0f（`AOI_CELL_SIZE`） | 根据客户端视野范围调整 |
| 坐标维度 | X / Z（忽略 Y 高度） | 适合 2D 俯视角或 3D 水平面 |
| 查询范围 | 3×3 = 9 格 | 以玩家所在格为中心的九宫格 |

---

## 源文件

```
src/apps/space/
├── aoi_component.h      # AOI 组件头文件
└── aoi_component.cpp    # AOI 组件实现
```

---

## 数据结构

```
AoiComponent
├── _cells:      HashMap<CellKey, HashSet<playerSn>>   格子 → 格子内的玩家集合
└── _playerCell: HashMap<playerSn, CellKey>            玩家 → 所在格子
```

```cpp
struct CellKey {
    int X;  // floor(worldPos.X / AOI_CELL_SIZE)
    int Z;  // floor(worldPos.Z / AOI_CELL_SIZE)
};
```

哈希函数：`hash(X) ^ (hash(Z) << 16)`，分布均匀且计算快。

---

## 接口设计

| 方法 | 说明 | 时间复杂度 |
|------|------|-----------|
| `Enter(playerSn, pos)` | 玩家进入场景，注册到对应格子 | O(1) |
| `Leave(playerSn)` | 玩家离开场景，从格子移除 | O(1) |
| `Move(playerSn, newPos)` | 玩家移动，跨格子时迁移 | O(1)（未跨格子时直接返回） |
| `GetNearbyPlayers(playerSn)` | 获取九宫格内所有玩家 | O(N)，N = 九宫格内玩家数 |
| `GetNearbyPlayers(pos)` | 获取某位置九宫格内所有玩家 | O(N) |

---

## 工作流程

### 玩家进入

```
Enter(playerSn=1001, pos={150, 0, 200})
  → CellKey = {3, 4}               // 150/50=3, 200/50=4
  → _cells[{3,4}].insert(1001)
  → _playerCell[1001] = {3, 4}
```

### 玩家移动（跨格子）

```
Move(playerSn=1001, newPos={160, 0, 260})
  → newKey = {3, 5}                 // 160/50=3, 260/50=5
  → oldKey = {3, 4}                 // 格子变了！
  → _cells[{3,4}].erase(1001)      // 从旧格子移除
  → _cells[{3,5}].insert(1001)     // 加入新格子
  → _playerCell[1001] = {3, 5}
```

### 玩家移动（未跨格子）

```
Move(playerSn=1001, newPos={155, 0, 205})
  → newKey = {3, 4}                 // 还是 {3,4}
  → oldKey == newKey → 直接返回，什么都不做
```

### 获取附近玩家

```
GetNearbyPlayers(playerSn=1001)
  → center = _playerCell[1001] = {3, 5}
  → 遍历 3×3：
      {2,4} {3,4} {4,4}
      {2,5} {3,5} {4,5}
      {2,6} {3,6} {4,6}
  → 收集所有格子内的 playerSn → 返回集合
```

---

## 架构位置

```
Space 进程
  └── World 组件
        └── AoiComponent（每个场景实例一个）
              ├── 玩家进入场景 → Enter()
              ├── MoveSystem 每帧 → Move()
              ├── 同步广播前 → GetNearbyPlayers()
              └── 玩家离开/下线 → Leave()
```

MoveSystem 每帧更新玩家位置后，调用 `AoiComponent::Move()` 检测是否跨格子。广播消息时，通过 `GetNearbyPlayers()` 获取接收者列表，只向九宫格内的玩家发包。

---

## 性能分析

### 优点

- **O(1) 的 Enter/Leave/Move**：哈希表操作，无需遍历全地图
- **只在跨格子时触发迁移**：格子内移动零开销
- **内存紧凑**：只存在有玩家的格子，空格子不占内存（空格子自动 erase）

### 复杂度对比

| 方案 | Enter | Move | 查询附近 | 适合规模 |
|------|-------|------|---------|---------|
| **九宫格（本实现）** | O(1) | O(1) | O(K)，K=附近玩家数 | 中小场景，千人级 |
| 暴力遍历 | O(1) | O(1) | O(N)，N=全场景玩家 | 几十人以下 |
| 十字链表 | O(N) | O(N) | O(K) | 大地图稀疏分布 |
| 四叉树 | O(logN) | O(logN) | O(K + logN) | 大世界动态密度 |

### 瓶颈与调优

| 场景 | 问题 | 解决方案 |
|------|------|---------|
| 格子过大 | 每次查询返回太多玩家，广播包量大 | 减小 `AOI_CELL_SIZE` |
| 格子过小 | 频繁跨格子，Move 操作多 | 增大 `AOI_CELL_SIZE` |
| 热点区域（城门口） | 单格子玩家过多 | 可配合距离二次过滤，或引入多级格子 |
| 超大地图 | 格子数量爆炸 | 本实现用 HashMap，不存空格子，无此问题 |

---

## 格子大小调优建议

```
AOI_CELL_SIZE ≈ 客户端视野半径 × 0.8 ~ 1.2
```

- 视野 = 40 单位 → 格子 = 40~50
- 视野 = 80 单位 → 格子 = 70~90

格子太大：广播浪费带宽。格子太小：频繁跨格触发 Move。取一个平衡值即可。

---

## 后续可扩展方向

1. **Enter/Leave 事件回调**：跨格子时自动通知新进入视野 / 离开视野的玩家，减少全量同步
2. **多级 AOI**：NPC/怪物用大格子（低频更新），玩家用小格子（高频更新）
3. **AOI 消息过滤**：结合 `MessageCallBackFilter` 只向 nearby 玩家投递包
4. **动态格子大小**：根据区域密度自动调整（类四叉树思路）

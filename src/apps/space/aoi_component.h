
#pragma once
#include "libserver/ecs/component.h"
#include "libserver/utils/vector3.h"
#include "libserver/ecs/system.h"
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <cmath>

// 格子大小（根据地图大小和客户端视野范围调整）
constexpr float AOI_CELL_SIZE = 50.0f;

// 格子坐标Key
struct CellKey
{
    int X;
    int Z;

    bool operator==(const CellKey& other) const
    {
        return X == other.X && Z == other.Z;
    }
};

// CellKey哈希函数
struct CellKeyHash
{
    size_t operator()(const CellKey& key) const
    {
        return std::hash<int>()(key.X) ^ (std::hash<int>()(key.Z) << 16);
    }
};

// AOI组件：基于九宫格的兴趣区域管理
class AoiComponent : public Component<AoiComponent>, public IAwakeFromPoolSystem<>
{
public:
    void Awake() override;
    void BackToPool() override;

    // 根据世界坐标计算格子坐标
    static CellKey GetCellKey(const Vector3& pos);

    // 玩家进入AOI
    void Enter(uint64 playerSn, const Vector3& pos);

    // 玩家离开AOI
    void Leave(uint64 playerSn);

    // 玩家移动，更新所在格子（如果跨格子了才更新）
    void Move(uint64 playerSn, const Vector3& newPos);

    // 获取某个玩家九宫格范围内的所有玩家SN
    std::set<uint64> GetNearbyPlayers(uint64 playerSn);

    // 获取某个位置九宫格范围内的所有玩家SN
    std::set<uint64> GetNearbyPlayers(const Vector3& pos);

private:
    // 格子 -> 格子内的玩家集合
    std::unordered_map<CellKey, std::unordered_set<uint64>, CellKeyHash> _cells;

    // 玩家 -> 所在格子
    std::unordered_map<uint64, CellKey> _playerCell;
};

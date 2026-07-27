
#include "aoi_component.h"

void AoiComponent::Awake()
{
    _cells.clear();
    _playerCell.clear();
}

void AoiComponent::BackToPool()
{
    _cells.clear();
    _playerCell.clear();
}

CellKey AoiComponent::GetCellKey(const Vector3& pos)
{
    return CellKey{
        static_cast<int>(std::floor(pos.X / AOI_CELL_SIZE)),
        static_cast<int>(std::floor(pos.Z / AOI_CELL_SIZE))
    };
}

void AoiComponent::Enter(uint64 playerSn, const Vector3& pos)
{
    CellKey key = GetCellKey(pos);
    _cells[key].insert(playerSn);
    _playerCell[playerSn] = key;
}

void AoiComponent::Leave(uint64 playerSn)
{
    auto it = _playerCell.find(playerSn);
    if (it == _playerCell.end())
        return;

    CellKey key = it->second;
    _cells[key].erase(playerSn);

    // 格子空了就移除
    if (_cells[key].empty())
        _cells.erase(key);

    _playerCell.erase(it);
}

void AoiComponent::Move(uint64 playerSn, const Vector3& newPos)
{
    auto it = _playerCell.find(playerSn);
    if (it == _playerCell.end())
    {
        // 玩家不在AOI中，直接加入
        Enter(playerSn, newPos);
        return;
    }

    CellKey oldKey = it->second;
    CellKey newKey = GetCellKey(newPos);

    // 格子没变，不需要更新
    if (oldKey == newKey)
        return;

    // 从旧格子移除
    _cells[oldKey].erase(playerSn);
    if (_cells[oldKey].empty())
        _cells.erase(oldKey);

    // 加入新格子
    _cells[newKey].insert(playerSn);
    _playerCell[playerSn] = newKey;
}

std::set<uint64> AoiComponent::GetNearbyPlayers(uint64 playerSn)
{
    std::set<uint64> result;

    auto it = _playerCell.find(playerSn);
    if (it == _playerCell.end())
        return result;

    CellKey center = it->second;

    // 遍历九宫格（自己所在格子 + 周围8个格子）
    for (int dx = -1; dx <= 1; dx++)
    {
        for (int dz = -1; dz <= 1; dz++)
        {
            CellKey neighbor{ center.X + dx, center.Z + dz };
            auto cellIt = _cells.find(neighbor);
            if (cellIt != _cells.end())
            {
                for (uint64 sn : cellIt->second)
                {
                    result.insert(sn);
                }
            }
        }
    }

    return result;
}

std::set<uint64> AoiComponent::GetNearbyPlayers(const Vector3& pos)
{
    std::set<uint64> result;
    CellKey center = GetCellKey(pos);

    for (int dx = -1; dx <= 1; dx++)
    {
        for (int dz = -1; dz <= 1; dz++)
        {
            CellKey neighbor{ center.X + dx, center.Z + dz };
            auto cellIt = _cells.find(neighbor);
            if (cellIt != _cells.end())
            {
                for (uint64 sn : cellIt->second)
                {
                    result.insert(sn);
                }
            }
        }
    }

    return result;
}

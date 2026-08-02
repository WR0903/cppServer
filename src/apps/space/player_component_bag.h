#pragma once
#include "libserver/ecs/component.h"
#include "libserver/ecs/system.h"
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
    int GetCapacity() const;
    const std::map<uint64, Proto::BagItem>& GetItems() const;

private:
    std::map<uint64, Proto::BagItem> _items;  // itemId -> item
    int _capacity{ 50 };
};

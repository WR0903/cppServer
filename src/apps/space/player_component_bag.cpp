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
    item.set_slot(static_cast<int>( _items.size()));
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

int PlayerComponentBag::GetCapacity() const
{
    return _capacity;
}

const std::map<uint64, Proto::BagItem>& PlayerComponentBag::GetItems() const
{
    return _items;
}

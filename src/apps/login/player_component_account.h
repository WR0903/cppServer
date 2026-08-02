#pragma once
#include "libserver/ecs/component.h"
#include "libserver/ecs/system.h"

class PlayerComponentAccount :public Component<PlayerComponentAccount>, public IAwakeFromPoolSystem<std::string>
{
public:
    void Awake(std::string password) override;
    void BackToPool() override;

    std::string GetPassword() const;

    void SetSelectPlayerSn(uint64 sn);
    uint64 GetSelectPlayerSn() const;

    void SetLastGameId(int gameId);
    int GetLastGameId() const;

private:
    std::string _password;
    uint64 _selectPlayerSn{ 0 };
    int _lastGameId{ 0 };
};


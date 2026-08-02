#pragma once
#include "pool/object_pool.h"
#include "network/packet.h"
#include "utils/singleton.h"

#include <mutex>

class DynamicPacketPool :public DynamicObjectPool<Packet>, public Singleton<DynamicPacketPool>
{
public:
    Packet* MallocPacket(Proto::MsgId msgId, NetIdentify* pIdentify);
    virtual void Update() override;
    virtual void FreeObject(IComponent* pObj) override;
    virtual void Show() override;

private:
    std::mutex _packet_lock;
};

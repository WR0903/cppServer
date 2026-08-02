#pragma once
#include "thread/thread_collector.h"

class ThreadCollectorExclusive :public ThreadCollector
{
public:
    ThreadCollectorExclusive(ThreadType threadType, int initNum);

    virtual void HandlerMessage(Packet* pPacket) override;
};


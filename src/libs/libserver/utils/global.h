#pragma once

#include "utils/common.h"
#include "utils/util_time.h"
#include "utils/app_type.h"
#include "utils/singleton.h"

#include <mutex>

class Global : public Singleton<Global>
{
public:
    Global(APP_TYPE appType, int appId);
    void UpdateTime();

    static int GetAppIdFromSN(uint64 sn);
    uint64 GenerateSN();	// SN = 64位,时间+服务器ID+ticket
    static std::string GenerateUUID();

    APP_TYPE GetCurAppType() const;
    int GetCurAppId() const;

    int YearDay;
    timeutil::Time TimeTick;

    bool IsStop{ false };

private:
    std::mutex _mtx;
    unsigned short _snTicket{ 1 };

    APP_TYPE _appType;
    int _appId{ 0 };
};


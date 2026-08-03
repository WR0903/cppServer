#pragma once

#include <cstdint>
#include <climits>
#include "protobuf/proto_id.pb.h"
#include "protobuf/db.pb.h"
#include "protobuf/msg.pb.h"

#ifndef PATH_MAX  
#define PATH_MAX 1024  
#endif  

#define INVALID_ID (-1)

#include <string.h>
#include <stdint.h>
#define engine_stricmp strcasecmp
#define engine_access access
#define engine_strncpy(dest, destsz, src, srcsz) strncpy(dest, src, srcsz)

#define SOCKET int
#define INVALID_SOCKET -1

typedef uint64_t	uint64;
typedef uint32_t	uint32;

#define BindFunP4(_self, _f) std::bind ( _f, _self, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4 )
#define BindFunP3(_self, _f) std::bind ( _f, _self, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3 )
#define BindFunP2(_self, _f) std::bind ( _f, _self, std::placeholders::_1, std::placeholders::_2 )
#define BindFunP1(_self, _f) std::bind ( _f, _self, std::placeholders::_1 )
#define BindFunP0(_self, _f) std::bind ( _f, _self)

// 打开跟踪数据（设为 1 开启）
// #define LOG_TRACE_COMPONENT_OPEN 1

// 打开效率测试（设为 1 开启）
#define LOG_EFFICIENCY_COMPONENT_OPEN 1

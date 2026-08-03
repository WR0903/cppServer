cmake_minimum_required(VERSION 3.1)

project(libserver C CXX)

# 收集 libserver 核心源码（根目录）
aux_source_directory(. LIB_SERVER_SRCS)
# 收集各功能子目录源码
aux_source_directory(./ecs LIB_SERVER_SRCS)
aux_source_directory(./pool LIB_SERVER_SRCS)
aux_source_directory(./network LIB_SERVER_SRCS)
aux_source_directory(./thread LIB_SERVER_SRCS)
aux_source_directory(./console LIB_SERVER_SRCS)
aux_source_directory(./log LIB_SERVER_SRCS)
aux_source_directory(./message LIB_SERVER_SRCS)
aux_source_directory(./cache LIB_SERVER_SRCS)
aux_source_directory(./redis LIB_SERVER_SRCS)
aux_source_directory(./utils LIB_SERVER_SRCS)
# 收集 protobuf 生成源码
aux_source_directory(./protobuf LIB_SERVER_SRCS)
# 将 mongoose.c 纳入 libserver 编译
list(APPEND LIB_SERVER_SRCS ${CMAKE_SOURCE_DIR}/src/3rdparty/mongoose/mongoose.c)

# protobuf 生成的 .pb.cc 文件有大量 deprecated 警告，单独用宽松编译选项
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/protobuf/msg.pb.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/protobuf/db.pb.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/protobuf/proto_id.pb.cc
    PROPERTIES COMPILE_FLAGS "-Wno-unused-parameter -Wno-missing-field-initializers"
)

# mongoose.c 是 C 文件，用宽松编译选项
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/3rdparty/mongoose/mongoose.c
    PROPERTIES COMPILE_FLAGS "-Wno-unused-parameter -Wno-unused-variable -Wno-format"
)

add_library(${PROJECT_NAME} STATIC ${LIB_SERVER_SRCS})

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_SOURCE_DIR}/src/libs
    ${CMAKE_SOURCE_DIR}/src/libs/libserver
    ${CMAKE_SOURCE_DIR}/src/libs/libserver/ecs
    ${CMAKE_SOURCE_DIR}/src/libs/libserver/pool
    ${CMAKE_SOURCE_DIR}/src/libs/libserver/network
    ${CMAKE_SOURCE_DIR}/src/libs/libserver/thread
    ${CMAKE_SOURCE_DIR}/src/libs/libserver/console
    ${CMAKE_SOURCE_DIR}/src/libs/libserver/log
    ${CMAKE_SOURCE_DIR}/src/libs/libserver/message
    ${CMAKE_SOURCE_DIR}/src/libs/libserver/cache
    ${CMAKE_SOURCE_DIR}/src/libs/libserver/redis
    ${CMAKE_SOURCE_DIR}/src/libs/libserver/utils
    ${CMAKE_SOURCE_DIR}/src/libs/libserver/protobuf
    ${CMAKE_SOURCE_DIR}/src/3rdparty
    ${CMAKE_SOURCE_DIR}/src/3rdparty/mongoose
)

# 通过 PUBLIC 传递所有第三方库依赖，app 只需链接 libserver 即可
target_link_libraries(${PROJECT_NAME} PUBLIC
    uuid
    libprotobuf
    jsoncpp
    yaml-cpp
    log4cplusS
    libcurl_static
    hiredis
    zlibstatic
)

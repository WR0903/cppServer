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
# 收集 mongoose 源码
aux_source_directory(./mongoose LIB_SERVER_SRCS)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../../libs)

set(CMAKE_CXX_FLAGS "-Wall -std=c++14 -pthread -DEPOLL -Wno-deprecated-declarations")
set(CMAKE_CXX_FLAGS_DEBUG "-D_DEBUG -O0 -g -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# protobuf 生成的 .pb.cc 文件有大量 deprecated 警告，单独用宽松编译选项
set(PB_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/protobuf/msg.pb.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/protobuf/db.pb.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/protobuf/proto_id.pb.cc
)

# mongoose.c 是 C 文件，用 C 编译器兼容选项
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/protobuf/msg.pb.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/protobuf/db.pb.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/protobuf/proto_id.pb.cc
    PROPERTIES COMPILE_FLAGS "-Wno-deprecated-declarations -Wno-unused-parameter -Wno-missing-field-initializers"
)

set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/mongoose/mongoose.c
    PROPERTIES COMPILE_FLAGS "-Wno-unused-parameter -Wno-unused-variable -Wno-format"
)

add_library(${PROJECT_NAME} STATIC ${LIB_SERVER_SRCS})

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ecs
    ${CMAKE_CURRENT_SOURCE_DIR}/pool
    ${CMAKE_CURRENT_SOURCE_DIR}/network
    ${CMAKE_CURRENT_SOURCE_DIR}/thread
    ${CMAKE_CURRENT_SOURCE_DIR}/console
    ${CMAKE_CURRENT_SOURCE_DIR}/log
    ${CMAKE_CURRENT_SOURCE_DIR}/message
    ${CMAKE_CURRENT_SOURCE_DIR}/cache
    ${CMAKE_CURRENT_SOURCE_DIR}/redis
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/protobuf
    ${CMAKE_CURRENT_SOURCE_DIR}/mongoose
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/libs
    ${CMAKE_SOURCE_DIR}/src/3rdparty
)

target_link_libraries(${PROJECT_NAME} PUBLIC uuid libprotobuf)

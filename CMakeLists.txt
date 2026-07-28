cmake_minimum_required(VERSION 3.1)

project(GameServer)

# 全局编译设置
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pthread -DEPOLL")
set(CMAKE_CXX_FLAGS_DEBUG "-D_DEBUG -O0 -g -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# ========== 第三方库源码编译（只编译一次） ==========

# jsoncpp
add_subdirectory(src/3rdparty/jsoncpp)

# yaml-cpp
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(src/3rdparty/yamlcpp)

# log4cplus
set(LOG4CPLUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(LOG4CPLUS_BUILD_LOGGINGSERVER OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(src/3rdparty/log4cplus)

# zlib（源码编译）
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "关闭 zlib 测试" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "只构建静态库" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "构建静态库" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "不安装" FORCE)
add_subdirectory(src/3rdparty/zlib-develop)

# protobuf（源码编译 3.21.12）
set(protobuf_BUILD_TESTS OFF CACHE BOOL "关闭测试" FORCE)
set(protobuf_BUILD_CONFORMANCE OFF CACHE BOOL "关闭 conformance" FORCE)
set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "关闭示例" FORCE)
set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "只构建静态库" FORCE)
set(protobuf_WITH_ZLIB OFF CACHE BOOL "关闭 zlib 支持（避免与源码 zlib 冲突）" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "不安装" FORCE)
add_subdirectory(src/3rdparty/protobuf-3.21.12)

# protobuf 是第三方库，自身代码在 -Wall 下会产生大量无害警告（如 sign-compare），
# 给其编译目标单独关闭警告，避免刷屏，且不影响业务代码自身的警告检查。
foreach(_pb_tgt libprotobuf libprotoc protoc)
    if(TARGET ${_pb_tgt})
        target_compile_options(${_pb_tgt} PRIVATE -w)
    endif()
endforeach()

# curl（源码编译 8.5.0）
set(BUILD_CURL_EXE OFF CACHE BOOL "不构建 curl 可执行文件" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "只构建静态库" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "构建静态库" FORCE)
set(CURL_USE_OPENSSL ON CACHE BOOL "使用 OpenSSL" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "关闭 LDAP" FORCE)
set(CURL_DISABLE_LDAPS ON CACHE BOOL "关闭 LDAPS" FORCE)
set(CURL_DISABLE_TESTS ON CACHE BOOL "关闭测试" FORCE)
set(PICKY_COMPILER OFF CACHE BOOL "关闭挑剔编译器选项" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "关闭 libPSL" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "关闭 libSSH2" FORCE)
set(USE_LIBIDN2 OFF CACHE BOOL "关闭 libidn2" FORCE)
add_subdirectory(src/3rdparty/curl-8.5.0)

# hiredis（源码编译）
set(BUILD_SHARED_LIBS OFF CACHE BOOL "只构建静态库" FORCE)
set(DISABLE_TESTS ON CACHE BOOL "关闭测试" FORCE)
set(ENABLE_SSL OFF CACHE BOOL "关闭 SSL" FORCE)
set(ENABLE_EXAMPLES OFF CACHE BOOL "关闭示例" FORCE)
add_subdirectory(src/3rdparty/hiredis)

# ========== 全局 3rdparty 头文件路径 ==========

include_directories(
    ${PROJECT_SOURCE_DIR}/src/3rdparty/jsoncpp/include
    ${PROJECT_SOURCE_DIR}/src/3rdparty/yamlcpp/include
    ${PROJECT_SOURCE_DIR}/src/3rdparty/log4cplus/include
    ${PROJECT_SOURCE_DIR}/src/3rdparty/curl-8.5.0/include
    ${PROJECT_SOURCE_DIR}/src/3rdparty
)

# ========== 全局链接搜索路径 ==========
# libmysqlclient.so 位于非标准路径 /usr/lib64/mysql，需显式加入链接搜索目录
link_directories(/usr/lib64/mysql)

# ========== 项目 lib（先编译静态库，避免各 app 重复编译相同源码） ==========

add_subdirectory(src/libs/libserver)
add_subdirectory(src/libs/libplayer)
add_subdirectory(src/libs/libresource)

# ========== 项目 app（各自链接静态库，不再重复编译库源码） ==========

add_subdirectory(src/apps/appmgr)
add_subdirectory(src/apps/login)
add_subdirectory(src/apps/dbmgr)
add_subdirectory(src/apps/game)
add_subdirectory(src/apps/space)
add_subdirectory(src/apps/allinone)
add_subdirectory(src/tools/robots)

cmake_minimum_required(VERSION 3.1)

project(libplayer CXX)

aux_source_directory(. LIB_PLAYER_SRCS)

add_library(${PROJECT_NAME} STATIC ${LIB_PLAYER_SRCS})

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_SOURCE_DIR}/src/libs/libplayer
)

target_link_libraries(${PROJECT_NAME} PUBLIC libserver)

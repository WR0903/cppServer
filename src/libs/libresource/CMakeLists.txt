cmake_minimum_required(VERSION 3.1)

project(libresource CXX)

aux_source_directory(. LIB_RESOURCE_SRCS)

add_library(${PROJECT_NAME} STATIC ${LIB_RESOURCE_SRCS})

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_SOURCE_DIR}/src/libs/libresource
)

target_link_libraries(${PROJECT_NAME} PUBLIC libserver)

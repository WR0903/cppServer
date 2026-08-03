cmake_minimum_required(VERSION 3.1)

if (CMAKE_BUILD_TYPE STREQUAL Release) 
	set(MyProjectName dbmgr)
else()
	set(CMAKE_BUILD_TYPE Debug)
	set(MyProjectName dbmgrd)
endif()

message(STATUS "ProjectName:${MyProjectName}")

project(${MyProjectName})

aux_source_directory(. SRCS)

add_executable(${MyProjectName} ${SRCS})

target_include_directories(${MyProjectName} PRIVATE
    ${CMAKE_SOURCE_DIR}/src/apps
)

# dbmgr 额外需要 mysqlclient 及其依赖
target_link_libraries(${MyProjectName} libserver mysqlclient pthread m ssl crypto dl)

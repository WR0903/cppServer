cmake_minimum_required(VERSION 3.1)

if (CMAKE_BUILD_TYPE STREQUAL Release) 
	set(MyProjectName space)
else()
	set(CMAKE_BUILD_TYPE Debug)
	set(MyProjectName spaced)
endif()

message(STATUS "ProjectName:${MyProjectName}")

project(${MyProjectName})

aux_source_directory(. SRCS)

add_executable(${MyProjectName} ${SRCS})

target_include_directories(${MyProjectName} PRIVATE
    ${CMAKE_SOURCE_DIR}/src/apps
)

target_link_libraries(${MyProjectName} libplayer libresource libserver)

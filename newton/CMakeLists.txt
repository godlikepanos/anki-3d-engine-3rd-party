file(GLOB_RECURSE NT_CORE_SOURCES core/*.cpp)
file(GLOB_RECURSE NT_MESH_UTIL_SOURCES meshUtil/*.cpp)
file(GLOB_RECURSE NT_NEWTON_SOURCES newton/*.cpp)
file(GLOB_RECURSE NT_PHYSICS_SOURCES physics/*.cpp)

#include_directories(${CMAKE_CURRENT_SOURCE_DIR})

if(LINUX)
	add_definitions("-D_POSIX_VER_64")
endif()

add_definitions("-msse4")

include_directories(core meshUtil newton physics)

add_library(ankinewton ${NT_CORE_SOURCES} ${NT_MESH_UTIL_SOURCES} ${NT_NEWTON_SOURCES} ${NT_PHYSICS_SOURCES})


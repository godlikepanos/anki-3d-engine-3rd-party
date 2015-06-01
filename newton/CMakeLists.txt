file(GLOB_RECURSE NT_CORE_SOURCES core/*.cpp)
file(GLOB_RECURSE NT_MESH_UTIL_SOURCES meshUtil/*.cpp)
file(GLOB_RECURSE NT_NEWTON_SOURCES newton/*.cpp)
file(GLOB_RECURSE NT_PHYSICS_SOURCES physics/*.cpp)

#include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(NT_PTHREAD_SOURCES "")

if(LINUX)
	add_definitions("-D_POSIX_VER_64")
elseif(WINDOWS)
	include_directories("pthreads.2")
	add_definitions("-DPTW32_STATIC_LIB -D_NEWTON_STATIC_LIB -D_MINGW_64_VER")
	set(NT_PTHREAD_SOURCES "pthreads.2/pthread.c")
endif()

include_directories("core" "meshUtil" "newton" "physics")

add_library(ankinewton ${NT_PTHREAD_SOURCES} ${NT_CORE_SOURCES} ${NT_MESH_UTIL_SOURCES} ${NT_PHYSICS_SOURCES} ${NT_NEWTON_SOURCES})


set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 ")

file(GLOB_RECURSE NT_CORE_SOURCES coreLibrary_300/source/core/*.cpp 
	coreLibrary_300/source/newton/*.cpp
	coreLibrary_300/source/physics/*.cpp
	coreLibrary_300/source/meshUtil/*.cpp)

file(GLOB_RECURSE NT_PACKAGE_SOURCES packages/dCustomJoints/*.cpp
	packages/dMath/*.cpp
	packages/dContainers/*.cpp
	packages/thirdParty/timeTracker/dTimeTracker.cpp)

include_directories("coreLibrary_300/source/newton"
	"coreLibrary_300/source/meshUtil/"
	"coreLibrary_300/source/physics/"
	"coreLibrary_300/source/core" 
	"packages/dContainers/"
	"packages/dMath/"
	"packages/dCustomJoints/"
	"packages/thirdParty/timeTracker")

add_library(ankinewton ${NT_CORE_SOURCES} ${NT_PACKAGE_SOURCES})



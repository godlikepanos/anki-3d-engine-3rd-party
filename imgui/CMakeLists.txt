file(GLOB_RECURSE SRC *.cpp)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Point to a couple of dirs because ImGUI's config points to anki headers
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../src)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/../..)

add_library(ankiimgui ${SRC})


file(GLOB_RECURSE SOURCE_FILES *.c)
include_directories(../khronos)
add_library(ankivolk ${SOURCE_FILES})

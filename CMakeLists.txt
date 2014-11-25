cmake_minimum_required(VERSION 2.8)

set(ANKI_EXTERN_SUB_DIRS tinyxml2 lua z)

if(LINUX OR MACOS OR WINDOWS)
	set(ANKI_EXTERN_SUB_DIRS ${ANKI_EXTERN_SUB_DIRS} GLEW)
endif()

if(ANKI_BUILD_TOOLS)
	set(ANKI_EXTERN_SUB_DIRS ${ANKI_EXTERN_SUB_DIRS} assimp)
endif()

if(${ANKI_WINDOW_BACKEND} STREQUAL "SDL")
	set(ANKI_EXTERN_SUB_DIRS ${ANKI_EXTERN_SUB_DIRS} SDL2)
endif()

foreach(TMP ${ANKI_EXTERN_SUB_DIRS})
	add_subdirectory(${TMP})
endforeach()


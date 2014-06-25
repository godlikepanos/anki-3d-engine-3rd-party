# General source files 
file(GLOB SDL_COMMON_SRC 
	# Required. It's the same as in SDL CMake
	src/*.c
	src/atomic/*.c
	src/audio/*.c
#	src/audio/dsp/*.c
#	src/audio/disk/*.c
#	src/audio/dummy/*.c
	src/cpuinfo/*.c
	src/events/*.c
	src/file/*.c
	src/libm/*.c
	src/render/*/*.c
	src/render/*.c
	src/stdlib/*.c
	src/thread/*.c
	src/thread/generic/*.c
	src/timer/*.c
	src/timer/dummy/*.c
	src/video/*.c
	src/video/dummy/*.c
	# Extra subsystems
	src/joystick/*.c
	src/haptic/*.c
	src/power/*.c
	src/loadso/*.c
	src/dynapi/*.c
	src/filesystem/*.c)

if(LINUX)
	add_definitions(-fvisibility=hidden)

	file(GLOB SDL_PLATFORM_SRC 
		#src/atomic/xxx/*.c
		#src/audio/xxx/*.c
		#src/core/*.c
		#src/cpuinfo/*.c
		#src/events/*.c
		#src/file/*.c
		#src/filesystem/*.c
		#src/libm/*.c
		#src/main/*.c
		#src/power/*.c
		#src/render/opengl/*.c
		#src/stdlib/*.c
		#src/test/*.c
		src/core/linux/*.c
		src/loadso/dlopen/*.c
		src/video/x11/*.c
		src/haptic/linux/*.c
		src/joystick/linux/*.c
#		src/thread/pthread/*.c
#		src/power/linux/*.c
#		src/timer/unix/*.c
		src/filesystem/unix/*.c)

		set(EXTRA_LIBS dl)
elseif(WINDOWS)
	add_definitions(-fvisibility=hidden)

	file(GLOB SDL_PLATFORM_SRC 
		src/core/windows/*.c
		src/loadso/windows/*.c
		src/video/windows/*.c
		src/haptic/dummy/*.c
		src/joystick/windows/*.c
		src/filesystem/windows/*.c
	)

	set(EXTRA_LIBS Imm32 Winmm)
elseif(MACOS)
	message(FATAL_ERROR "TODO")
else()
	message(FATAL_ERROR "Shouldn't reach that point")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(ankisdl ${SDL_COMMON_SRC} ${SDL_PLATFORM_SRC})
target_link_libraries(ankisdl m ${EXTRA_LIBS})

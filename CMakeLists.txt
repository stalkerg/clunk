cmake_minimum_required(VERSION 3.10)
project(clunk)
set(CMAKE_USE_RELATIVE_PATHS TRUE)
include(CheckCXXSymbolExists)

SET(CMAKE_CONFIGURATION_TYPES "Release;Debug;MinSizeRel;RelWithDebInfo")

find_package(SDL3 3.2 REQUIRED CONFIG REQUIRED COMPONENTS SDL3)
option(WITH_SSE "Use highly optimized SSE FFT/MDCT routines" false)

# Build clunk as a shared library by default
set(BUILD_SHARED_LIBS ON CACHE BOOL INTERNAL)

# For install like macports for easy dylibbundler
set(CMAKE_SKIP_RPATH true)
set(CMAKE_INSTALL_NAME_DIR "${CMAKE_INSTALL_PREFIX}/lib")

set(clunk_VERSION_MAJOR 1)
set(clunk_VERSION_MINOR 0)

if(WIN32)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_WINDOWS)
endif(WIN32)

add_definitions(-DCLUNKAPI=DLLEXPORT)
set(CMAKE_INCLUDE_CURRENT_DIR TRUE)

set(SOURCES 
	buffer.cpp
	clunk_ex.cpp
	context.cpp
	distance_model.cpp
	kemar.c
	locker.cpp
	logger.cpp
	object.cpp
	sample.cpp
	sdl_ex.cpp
	source.cpp
	stream.cpp
	clunk_c.cpp
)
set(PUBLIC_HEADERS
	buffer.h
	clunk.h
	clunk_assert.h
	context.h
	distance_model.h
	export_clunk.h
	fft_context.h
	locker.h
	logger.h
	mdct_context.h
	object.h
	sample.h
	source.h
	sse_fft_context.h
	stream.h
	v3.h
	clunk_c.h
)

if (MSVC)
	add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>")
else()
	SET(CMAKE_CXX_FLAGS_DEBUG "-g -Wall -Wextra")
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -ftree-vectorize -ffast-math")
endif()
if (WITH_SSE)
	set(SOURCES ${SOURCES} sse_fft_context.cpp)
	add_definitions(-DCLUNK_USES_SSE)
	if (NOT MSVC)
		add_definitions(-msse2)
	endif()
endif(WITH_SSE)

check_cxx_symbol_exists(pow10f "math.h" HAVE_POW10F_FUNCTION)
check_cxx_symbol_exists(log2f "math.h" HAVE_LOG2F_FUNCTION)

if(NOT HAVE_POW10F_FUNCTION)
	add_definitions(-DNEED_POW10F)
endif()

if(NOT HAVE_LOG2F_FUNCTION)
	add_definitions(-DNEED_LOG2F)
endif()

message(STATUS "Build clunk as a shared library: ${BUILD_SHARED_LIBS}")
add_library(clunk ${SOURCES})
target_include_directories(clunk PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
	$<INSTALL_INTERFACE:include/clunk>
)

install(TARGETS clunk
	RUNTIME DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
)
install(FILES ${PUBLIC_HEADERS} DESTINATION include/clunk)

target_link_libraries(clunk PUBLIC SDL3::SDL3)

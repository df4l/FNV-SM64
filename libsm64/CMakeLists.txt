# Builds the sm64-san-andreas libsm64 fork as a 32-bit libsm64.dll with MSVC.
# The fork is left untouched: its src/ is copied into the build tree and
# libsm64-msvc.patch is applied there. See FNV-SM64-RE/notes/build.md.
cmake_minimum_required(VERSION 3.20)
project(libsm64 C)

set(SM64_FORK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/sm64-san-andreas/libsm64" CACHE PATH "sm64-san-andreas libsm64 fork")
set(SM64_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/libsm64-msvc.patch")
set(SRC "${CMAKE_BINARY_DIR}/src")

find_package(Git REQUIRED)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SM64_PATCH}")
file(SHA1 "${SM64_PATCH}" patch_hash)
set(stamp "${SRC}/.msvc-patch")
if(EXISTS "${stamp}")
	file(READ "${stamp}" applied_hash)
endif()
if(NOT applied_hash STREQUAL patch_hash)
	file(REMOVE_RECURSE "${SRC}")
	file(COPY "${SM64_FORK_DIR}/src" DESTINATION "${CMAKE_BINARY_DIR}")
	# The ceiling stops git from finding an enclosing repo (Desktop\NVSE), so
	# the patch paths resolve relative to ${SRC}.
	execute_process(
		COMMAND ${CMAKE_COMMAND} -E env "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}"
			${GIT_EXECUTABLE} apply --whitespace=nowarn "${SM64_PATCH}"
		WORKING_DIRECTORY "${SRC}"
		RESULT_VARIABLE patch_result)
	if(patch_result)
		message(FATAL_ERROR "libsm64-msvc.patch failed to apply to ${SM64_FORK_DIR}/src")
	endif()
	file(WRITE "${stamp}" "${patch_hash}")
endif()

file(GLOB SM64_SOURCES
	${SRC}/*.c
	${SRC}/decomp/*.c
	${SRC}/decomp/engine/*.c
	${SRC}/decomp/game/*.c
	${SRC}/decomp/tools/*.c
	${SRC}/decomp/audio/*.c
	${SRC}/decomp/pc/*.c
	${SRC}/decomp/mario/*.c
)

add_library(sm64 SHARED ${SM64_SOURCES})
set_target_properties(sm64 PROPERTIES OUTPUT_NAME libsm64 C_STANDARD 11)
target_include_directories(sm64 PRIVATE ${SRC} ${SRC}/decomp/include ${SRC}/decomp/mario)
target_compile_definitions(sm64 PRIVATE SM64_LIB_EXPORT VERSION_US NO_SEGMENTED_MEMORY GBI_FLOATS _CRT_SECURE_NO_WARNINGS)
# Decomp noise: GCC pragmas, min/max redefinition, intentional bogus returns in math_util.c.
target_compile_options(sm64 PRIVATE /wd4068 /wd4005 /wd4172 /wd4477 /wd4716)
# Static CRT: the DLL depends on KERNEL32 only.
set_property(TARGET sm64 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Install next to the plugin: Data\NVSE\Plugins\sm64\libsm64.dll
if(DEFINED ENV{FalloutNVPath})
	add_custom_command(TARGET sm64 POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{FalloutNVPath}/Data/NVSE/Plugins/sm64"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:sm64>" "$ENV{FalloutNVPath}/Data/NVSE/Plugins/sm64/")
endif()

cmake_minimum_required (VERSION 3.20)

project("VisTrace" CXX)
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

find_package(OpenMP QUIET)

unset(MSVC) # Unset MSVC var to force the library to compile with OpenMP under clang with the MSVC toolset
add_subdirectory("libs/bvh")
set(MSVC 1)

add_subdirectory("libs/glm")
add_subdirectory("libs/BSPParser")
add_subdirectory("libs/VTFParser")
add_subdirectory("libs/GMFS")

add_library(
	${PROJECT_NAME} SHARED
	"source/VisTrace.cpp"
	"source/Utils.cpp"

	"source/objects/Sampler.cpp"

	"source/objects/RenderTarget.cpp"
	"source/objects/HDRI.cpp"
	"source/objects/VTFTexture.cpp"

	"source/objects/TraceResult.cpp"
	"source/objects/AccelStruct.cpp"

	"source/libraries/BSDF.cpp"
	"source/libraries/Tonemapper.cpp"
)

target_include_directories(
	${PROJECT_NAME} PRIVATE
	"source"
	"source/objects"
	"source/libraries"

	"include"

	"libs/BSPParser"
	"libs/VTFParser"
	"libs/GMFS"

	"libs/bvh/include"
	"libs/glm"
	"libs/stb"
	"libs/gmod-module-base/include"
	"libs/yocto-gl/libs/yocto"
)

if (OpenMP_CXX_FOUND)
	target_link_libraries(
		${PROJECT_NAME} PRIVATE
		OpenMP::OpenMP_CXX
		libomp.lib
	)
endif()

target_link_libraries(
	${PROJECT_NAME} PRIVATE
	BSPParser
	bvh
	glm
	VTFParser
	GMFS
)

cmake_minimum_required (VERSION 3.20)

project("VisTrace" CXX)
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if (USE_OPENMP)
	find_package(OpenMP QUIET)
else()
	set(MSVC 1) # Set MSVC to disable BVH from using OpenMP
endif()

add_subdirectory("libs/bvh")

if (NOT USE_OPENMP)
	unset(MSVC)
endif()

add_subdirectory("libs/glm")
add_subdirectory("libs/BSPParser")
add_subdirectory("libs/VTFParser")
add_subdirectory("libs/GMFS")

set(BINARY_NAME gmcl_${PROJECT_NAME}-v${VISTRACE_API_VERSION}_${BINARY_SUFFIX})

add_library(
	${BINARY_NAME} SHARED
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
	${BINARY_NAME} PRIVATE
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

if (OpenMP_CXX_FOUND AND USE_OPENMP)
	target_link_libraries(
		${BINARY_NAME} PRIVATE
		OpenMP::OpenMP_CXX
		libomp.lib
	)
endif()

target_link_libraries(
	${BINARY_NAME} PRIVATE
	BSPParser
	bvh
	glm
	VTFParser
	GMFS
)

set_target_properties(${BINARY_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/release/")

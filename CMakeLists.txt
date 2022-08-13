cmake_minimum_required (VERSION 3.20)

project("VisTrace" CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

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

	"source/objects/TraceResult.cpp"
	"source/objects/AccelStruct.cpp"

	"source/objects/BSDF.cpp"
	"source/objects/HDRI.cpp"
	"source/objects/Tonemapper.cpp"
)

target_include_directories(
	${PROJECT_NAME} PRIVATE
	"source"
	"source/objects"
	"include/vistrace"
	"libs/BSPParser"
	"libs/bvh/include"
	"libs/glm"
	"libs/stb"
	"libs/VTFParser"
	"libs/gmod-module-base/include"
	"libs/GMFS/source"
)

target_link_libraries(
	${PROJECT_NAME}
	BSPParser
	bvh
	glm
	VTFParser
	${GMFS_LIBS}
)

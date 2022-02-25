PROJECT_GENERATOR_VERSION = 3

include("./libs/garrysmod_common")

CreateWorkspace({name = "VisTrace", abi_compatible = true, path = "projects/" .. os.target() .. "/" .. _ACTION})

include("./libs/VTFParser/premake5_include.lua")

CreateProject({serverside = false, source_path = "source"})
IncludeLuaShared()
IncludeScanning()
IncludeDetouring()
IncludeHelpersExtended()
IncludeSDKCommon()
IncludeSDKTier0()
IncludeSDKTier1()

includedirs({
	"./libs/glm",
	"./libs/bvh/include",
	"./libs/VTFParser",
	"./libs/stb"
})

links("VTFParser")

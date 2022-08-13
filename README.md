<p align="center">
<img src="https://github.com/Derpius/VisTrace/blob/master/Branding/banner.png?raw=true" />
</p>

VisTrace allows tracing visual meshes in Garry's Mod at high speeds on the CPU using https://github.com/madmann91/bvh, allowing for far higher fidelity scenes in Lua tracers without the massive performance cost of Lua mesh intersection.  

## Installation
Simply get the latest binary for your architecture from releases, and place it into `garrysmod/lua/bin`, then `require("VisTrace-vX.X")` in GLua.  
For a more user friendly experience, get the [Steam Workshop addon](https://steamcommunity.com/sharedfiles/filedetails/?id=2531198548), which will automatically require the applicable version of the module, and provide integration with other addons (currently [StarfallEx](https://github.com/thegrb93/StarfallEx) ~~and [Expression 2](https://github.com/wiremod/wire)~~ https://github.com/Derpius/VisTrace/issues/22).  

## Compiling
While it's recommended to just download the latest release binary, if you want to test WIP features before a full release, or you want to help develop the module, then you'll need to set up your toolchain to compile VisTrace and its dependencies.  

### Prerequesits
* Visual Studio 2019 or Visual Studio Code with the CMake tools extension and Visual Studio 2019 Build Tools installed
* clang-cl - MSVC version of Clang to use OpenMP while being ABI compatible with source (you can install this as part of the Visual Studio build tools)
* Ninja (again can be installed as part of the VS build tools, you could likely build using another system, but untested)
* CMake
* premake5 installed and on path (for building GMFS)
* msbuild installed and on path (for building GMFS, part of the VS build tools)

The GMFS submodule is not set up to invoke premake with anything other than vs2019, so you'll need to modify its CMakeLists.txt if you want to compile with an older version of VS, or on Linux.  
If you do make such modifications in addition to still letting the file work on Windows with vs2019, then please submit a PR with your new CMakeLists.txt at https://github.com/Derpius/GMFS.  

### First time setup
1. Clone the repository with the `--recursive` flag to init all submodules (check that submodules of submodules like `sourcedk-minimal` in `garrysmod_common` in `GMFS` have files in them)
2. Open the `Module` directory in either vs2019 or vscode (other editors can probably be used but dont have as good of an integration with CMake)  
3. Select a preset to compile (`relwithsymbols` for debugging as building with full debug mode breaks ABI compatibility with Source)
4. Compile (compiled dll can be found in `Module/out/build/{presetname}`)

## Extensions
VisTrace versions v0.10.0 and newer support user made extensions that can interface with VisTrace's objects via interfaces in `Module/include`, or using the GLua API.  
You should `require()` any binary modules and initialise your extension from the `VisTraceInit` hook, which will be called by the binary if everything loaded correctly.  

Additionally, an svg badge is provided to include in your readmes to show that your addon/binary module is VisTrace compatible:  
| Markdown | Preview |
|----------|---------|
| `[![VisTrace EXTENSION](https://github.com/Derpius/VisTrace/blob/master/Branding/extension.svg?raw=true)](https://github.com/Derpius/VisTrace)` | [![VisTrace EXTENSION](https://github.com/Derpius/VisTrace/blob/master/Branding/extension.svg?raw=true)](https://github.com/Derpius/VisTrace) |

## Usage
MOVING TO WIKI

## Example Code
For more detailed examples, see the [Examples](https://github.com/Derpius/VisTrace/tree/master/Examples) folder.  

This will load the module, build the acceleration structure from all `prop_physics` entities (and the world), get an entity to use as a hit marker, and traverse the scene each frame placing the hit marker at the trace hit position if we hit:
```lua
-- Instead of manually checking realm/vistrace version, and requiring by hand
-- You could use the VisTraceInit hook which will be called by the binary if
-- everything loaded OK (this however needs the VisTrace addon to be installed)
if SERVER then error("VisTrace can only be used on the client!") end
require("VisTrace-vX.X") -- Put current version here
local accel = vistrace.CreateAccel(ents.FindByClass("prop_physics")--[[, false]]) -- Pass false here to disable tracing world (useful if you just want to interact with entities)

local plr = LocalPlayer()

-- Change the entity ID here to one you want to use as a hit marker
-- hard coded here for simplicity of the example,
-- and assuming no addons that change this are mounted,
-- will be the first prop created on flatgrass in singleplayer
local hitMarker = Entity(68) 


hook.Add("Think", "vistraceTest", function()
	local hitData = accel:Traverse(plr:EyePos(), plr:GetAimVector())
	if hitData then
		hitMarker:SetPos(hitData:Pos())
	end
end)
```

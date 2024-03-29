<p align="center">
	<a href="https://steamcommunity.com/sharedfiles/filedetails/?id=2531198548" style="text-decoration: none;">
		<img alt="Banner" src="https://github.com/Derpius/VisTrace/blob/branding/banner.png?raw=true" />
	</a><br>
	<a href="https://github.com/Derpius/VisTrace/actions/workflows/build.yml" style="text-decoration: none;">
		<img alt="GitHub Workflow Status" src="https://img.shields.io/github/actions/workflow/status/Derpius/VisTrace/build.yml?branch=master&logo=c%2B%2B&style=for-the-badge" />
	</a>
	<a href="https://github.com/Derpius/VisTrace/issues" style="text-decoration: none;">
		<img alt="GitHub Open Issues" src="https://img.shields.io/github/issues/Derpius/VisTrace?logo=github&style=for-the-badge" />
	</a>
	<a href="https://discord.gg/N4NxezCgFQ" style="text-decoration: none;">
		<img alt="Discord" src="https://img.shields.io/discord/1020968021289873418?color=%237289DA&label=Discord&logo=discord&logoColor=white&style=for-the-badge" />
	</a><br>
	<a href="https://github.com/Derpius/VisTrace/releases/latest" style="text-decoration: none;">
		<img alt="GitHub Release Downloads" src="https://img.shields.io/github/downloads/Derpius/VisTrace/total?logo=github&style=for-the-badge">
	</a>
	<a href="https://steamcommunity.com/sharedfiles/filedetails/?id=2531198548" style="text-decoration: none;">
		<img alt="Steam Workshop Subscribers" src="https://img.shields.io/steam/subscriptions/2531198548?label=SUBSCRIBERS&logo=steam&style=for-the-badge" />
	</a>
</p>

---

VisTrace allows tracing visual meshes in Garry's Mod at high speeds on the CPU using https://github.com/madmann91/bvh, allowing for far higher fidelity scenes in Lua tracers without the massive performance cost of Lua mesh intersection.  

## Installation
Simply get the latest binary for your architecture from releases, and place it into `garrysmod/lua/bin`, then `require("VisTrace-vX.X")` in GLua.  
For a more user friendly experience, get the [Steam Workshop addon](https://steamcommunity.com/sharedfiles/filedetails/?id=2531198548), which will automatically require the applicable version of the module, and provide integration with other addons (currently [StarfallEx](https://github.com/thegrb93/StarfallEx) ~~and [Expression 2](https://github.com/wiremod/wire)~~ https://github.com/Derpius/VisTrace/issues/22).  

## Compiling
While it's recommended to just download the latest release binary, if you want to test WIP features before a full release, or you want to help develop the module, then you'll need to set up your toolchain to compile VisTrace and its dependencies.  

### Prerequesits
* CMake 3.20 or newer
* Ninja
* clang - Clang with MSVC backend to use OpenMP while being ABI compatible with source

### First time setup
1. Clone the repository with the `--recursive` flag to init all submodules
2. Open the cloned folder in your editor of choice
3. Select a preset to compile (`relwithsymbols` for debugging as building with full debug mode breaks ABI compatibility with Source)
4. Compile (compiled dll can be found in `out/build/{presetname}`)

## Extensions
VisTrace versions v0.10.0 and newer support user made extensions that can use and extend VisTrace's objects via interfaces in `include`.  
A quick start repository template is available [here](https://github.com/Derpius/VisTraceExtension)

Additionally, an svg badge is provided to include in your readmes to show that your addon/binary module is VisTrace compatible:  
| Markdown | Preview |
|----------|---------|
| `[![VisTrace EXTENSION](https://github.com/Derpius/VisTrace/blob/branding/extension.svg?raw=true)](https://github.com/Derpius/VisTrace)` | [![VisTrace EXTENSION](https://github.com/Derpius/VisTrace/blob/branding/extension.svg?raw=true)](https://github.com/Derpius/VisTrace) |

## Showcase
If you'd like to submit any images/videos showcasing your use of VisTrace, please submit a PR/Issue on the [branding branch](https://github.com/Derpius/VisTrace/tree/branding).  

Please note that any content submitted may be used on VisTrace's GitHub, and Workshop pages, as well as the website.  
See the readme on the branding branch for the naming convention.  

## Usage
MOVING TO WIKI

## Example Code
~~For more detailed examples, see the [Examples](https://github.com/Derpius/VisTrace/tree/master/Examples) folder.~~  
Will be moving to a dedicated branch

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

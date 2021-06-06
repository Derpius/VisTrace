# VisTrace  
VisTrace allows tracing visual meshes in Garry's Mod at high speeds on the CPU using https://github.com/madmann91/bvh, allowing for far higher fidelity scenes in Lua tracers without the massive performance cost of Lua mesh intersection.  

## Installation
Simply get the latest binary for your architecture from releases, and place it into `garrysmod/lua/bin`, then `require("VisTrace")` in GLua.  

## Usage
VisTrace provides 2 functions in the global scope:
* `vistrace.RebuildAccel(table entities = {})`
* `vistrace.TraverseScene(Vector origin, Vector direction, float tMin = 0, float tMax = FLT_MAX, hitWorld = true)`.  
  
`RebuildAccel` takes an optional sequential numerical table of entities to build mesh data from, and creates the BVH acceleration structure.  
This should be called as **infrequently** as possible, due to the time required to build an acceleration structure (i.e. once per frame for a tracer).  

`TraverseScene` takes at a minimum an origin and direction for the ray, with optional parameters to set the minimum and maximum hit distances, and whether or not to call `util.TraceLine` internally in order to hit the world (defaults to `true` as that's most likely the expected behaviour, however not hitting world is significantly faster).  
Attempting to call `TraverseScene` before acceleration structures have been built will throw a Lua error.  
The return value of this function is a [`TraceResult`](https://wiki.facepunch.com/gmod/Structures/TraceResult) struct, with the following additional contents:
* `HitU` is the texture coord u at the hit point (always 0 if the world was hit)  
* `HitV` is the texture coord v at the hit point (always 0 if the world was hit)  
* `HitTangent` is the tangent at the hit point (always Vector(0) if the world was hit, sometimes Vector(0) if not present in MeshVertex structs)  
* `HitBinormal` is the binormal at the hit point (always Vector(0) if the world was hit, sometimes Vector(0) if not present in MeshVertex structs)  

Note that if a mesh was hit, the majority of the `TraceResult` struct returned will not differ from the default values present in a miss struct, like `FractionLeftSolid`.  

## Example Code
This will load the module, build the acceleration structure from all `prop_physics` entities, get an entity to use as a hit marker, and traverse the scene each frame placing the hit marker at the trace hit position if applicable:
```lua
if SERVER then error("VisTrace can only be used on the client!") end
require("VisTrace")
vistrace.RebuildAccel(ents.FindByClass("prop_physics"))

local plr = LocalPlayer()

local hitMarker = Entity(68) -- Change the entity ID here to one you want to use as a hit marker (hard coded here for simplicity of the example, and assuming no addons that change this will be the first prop created on flatgrass in singleplayer)

hook.Add("Think", "vistraceTest", function()
	local hitData = vistrace.TraverseScene(plr:EyePos(), plr:GetAimVector())
	if hitData.Hit then
		hitMarker:SetPos(hitData.HitPos)
	end
end)
```

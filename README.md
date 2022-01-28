# VisTrace  
VisTrace allows tracing visual meshes in Garry's Mod at high speeds on the CPU using https://github.com/madmann91/bvh, allowing for far higher fidelity scenes in Lua tracers without the massive performance cost of Lua mesh intersection.  

## Installation
Simply get the latest binary for your architecture from releases, and place it into `garrysmod/lua/bin`, then `require("VisTrace-vX.X")` in GLua.  
For a more user friendly experience, get the [Steam Workshop addon](https://steamcommunity.com/sharedfiles/filedetails/?id=2531198548), which will automatically require the applicable version of the module, and provide integration with other addons (currently [StarfallEx](https://github.com/thegrb93/StarfallEx) and [Expression 2](https://github.com/wiremod/wire)).  

## Usage
VisTrace provides a function in the global scope in order to create an acceleration structure object:
* `vistrace.CreateAccel(table entities = {})`  
  
`CreateAccel` takes an optional sequential numerical table of entities to build mesh data from, creates the BVH acceleration structure, and returns an `AccelStruct` object.  
The `AccelStruct` object has a basic `__tostring` metamethod (which simply returns `"AccelStruct"`), and two main methods:  
* `AccelStruct:Rebuild(table entities = {})` - Same as `vistrace.CreateAccel` but self modifies.  
This should be called as **infrequently** as possible, due to the time required to build an acceleration structure (i.e. once per frame for a tracer).  
* `AccelStruct:Traverse(Vector origin, Vector direction, float tMin = 0, float tMax = FLT_MAX, hitWorld = true, hitWater = false)` - Traverses the acceleration structure, returning a [`TraceResult`](https://wiki.facepunch.com/gmod/Structures/TraceResult) table, with a few extra values.  

### `AccelStruct:Traverse` Details
`Traverse` takes at a minimum an origin and direction for the ray, with optional parameters to set the minimum and maximum hit distances, and whether or not to call `util.TraceLine` internally in order to hit the world and/or water (`hitWorld` defaults to `true` and `hitWater` to `false` as that's most likely the expected behaviour, however not hitting world/water is significantly faster).  

The return value of this method is a [`TraceResult`](https://wiki.facepunch.com/gmod/Structures/TraceResult) struct, with the following additional contents:
* `HitShader` is a table containing shader data at the hit point (currently `Albedo`, `Alpha` and `Material` which is the material object itself)
* `HitTexCoord` is a table representing the texture coord at the hit point (always `{u = 0, v = 0}` if the world was hit)  
* `HitBarycentric` is a table representing the barycentric coord local to the tri at the hit point (always `{u = 0, v = 0}` if the world was hit)  
* `HitTangent` is the tangent at the hit point (always Vector(0) if the world was hit, sometimes Vector(0) if not present in MeshVertex structs)  
* `HitBinormal` is the binormal at the hit point (always Vector(0) if the world was hit, sometimes Vector(0) if not present in MeshVertex structs)  
* `HitNormalGeometric` is the geometric normal of the hit (same as `HitNormal` if world was hit)  
* `SubmatIndex` is the id of the submaterial hit (always 0 if world was hit, will be the 0 indexed id used with `Entity:GetSubMaterial`, so add 1 if needed for other uses)  
* `EntIndex` is the id of the entity hit (this is required as an entity can be built into the accel struct, deleted, and replaced with a new entity at the same id, which now correctly marks the ent as null in the `TraceResult`, but you can still get the original id to index a table of custom data you might have)  

Note that if a mesh was hit, the majority of the `TraceResult` struct returned will not differ from the default values present in a miss struct, like `FractionLeftSolid`.  

## Example Code
For more detailed examples, see the [Examples](https://github.com/Derpius/VisTrace/tree/master/Examples) folder.  

This will load the module, build the acceleration structure from all `prop_physics` entities, get an entity to use as a hit marker, and traverse the scene each frame placing the hit marker at the trace hit position if applicable:
```lua
if SERVER then error("VisTrace can only be used on the client!") end
require("VisTrace")
local accel = vistrace.CreateAccel(ents.FindByClass("prop_physics"))

local plr = LocalPlayer()

local hitMarker = Entity(68) -- Change the entity ID here to one you want to use as a hit marker (hard coded here for simplicity of the example, and assuming no addons that change this will be the first prop created on flatgrass in singleplayer)

hook.Add("Think", "vistraceTest", function()
	local hitData = accel:Traverse(plr:EyePos(), plr:GetAimVector())
	if hitData.Hit then
		hitMarker:SetPos(hitData.HitPos)
	end
end)
```

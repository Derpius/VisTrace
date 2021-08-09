local inf = math.huge

local function validateVector(v)
	if (
		v[1] ~= v[1] or v[1] == inf or v[1] == -inf or
		v[2] ~= v[2] or v[2] == inf or v[2] == -inf or
		v[3] ~= v[3] or v[3] == inf or v[3] == -inf
	) then SF.Throw("Invalid vector, inf or NaN present in function traverseScene", 3) end
end

local checkLuaType = SF.CheckLuaType
local debug_getmetatable = debug.getmetatable

SF.Permissions.registerPrivilege("vistrace", "VisTrace", "Allows the user to build acceleration structures and traverse scenes", { client = {default = 1} })

--- Constructs and traverses a BVH acceleration structure on the CPU allowing for high speed vismesh intersections
--- Requires the binary module installed to use, which you can get here https://github.com/Derpius/VisTrace/releases
-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
-- @name vistrace
-- @class library
-- @libtbl vistrace_library
SF.RegisterLibrary("vistrace")

--- VisTrace acceleration structure
-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
-- @name AccelStruct
-- @class type
-- @libtbl accelstruct_methods
-- @libtbl accelstruct_meta
SF.RegisterType("AccelStruct", true, false, debug.getregistry().AccelStruct)

return function(instance)
	local checkPermission = instance.player ~= SF.Superuser and SF.Permissions.check or function() end
	local vistrace_library = instance.Libraries.vistrace

	local accelstruct_methods, accelstruct_meta = instance.Types.AccelStruct.Methods, instance.Types.AccelStruct
	local wrapAccel, uwrapAccel = instance.Types.AccelStruct.Wrap, instance.Types.AccelStruct.Unwrap

	local uwrapEnt, uwrapVec, wrapObj = instance.Types.Entity.Unwrap, instance.Types.Vector.Unwrap, instance.WrapObject
	local entMetaTable, vecMetaTbl = instance.Types.Entity, instance.Types.Vector

	function accelstruct_meta.__tostring()
		return "AccelStruct"
	end

	local function canRun()
		checkPermission(instance, nil, "vistrace")
		if not vistrace then
			SF.Throw("The required version (v0.4.x) of the VisTrace binary module is not installed (get it here https://github.com/Derpius/VisTrace/releases)", 3)
		end
	end

	--- Rebuild the acceleration structure
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param table? entities Sequential list of entities to rebuild the acceleration structure with (or nil to clear the structure)
	function accelstruct_methods:rebuild(entities)
		canRun()
		if entities then
			checkLuaType(entities, TYPE_TABLE)
			local unwrapped = {}
			for k, v in pairs(entities) do
				if debug_getmetatable(v) ~= entMetaTable then SF.ThrowTypeError("Entity", SF.GetType(v), 2, "Entity table entry not an entity.") end
				unwrapped[k] = uwrapEnt(v)
			end
			entities = unwrapped
		end
		uwrapAccel(self):Rebuild(entities)
	end

	--- Traverses the acceleration structure
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param Vector origin Ray origin
	-- @param Vector direction Ray direction
	-- @param number? tMin Minimum distance of the ray (basically offset from start along direction)
	-- @param number? tMax Maximum distance of the ray
	-- @param boolean? hitWorld Enables calling util.TraceLine internally to hit the world (default: true)
	-- @param boolean? hitWater Enables calling util.TraceLine internally to hit water (default: false)
	-- @return table Result of the traversal as a TraceResult struct with some extra values (see https://github.com/100PXSquared/VisTrace#usage)
	function accelstruct_methods:traverse(origin, direction, tMin, tMax, hitWorld, hitWater)
		canRun()

		if debug_getmetatable(origin) ~= vecMetaTbl then SF.ThrowTypeError("Vector", SF.GetType(origin), 2) end
		validateVector(origin)

		if debug_getmetatable(direction) ~= vecMetaTbl then SF.ThrowTypeError("Vector", SF.GetType(direction), 2) end
		validateVector(direction)

		if tMin then checkLuaType(tMin, TYPE_NUMBER) end
		if tMax then checkLuaType(tMax, TYPE_NUMBER) end
		if hitWorld then checkLuaType(hitWorld, TYPE_BOOL) end
		if hitWater then checkLuaType(hitWater, TYPE_BOOL) end

		local hitData = uwrapAccel(self):Traverse(uwrapVec(origin), uwrapVec(direction), tMin, tMax, hitWorld, hitWater)
		for k, v in pairs(hitData) do -- Note that vistrace returns tables, not actual TraceResult structs, so we can just enumerate and wrap rather than using SF.StructWrapper
			if k == "HitTexCoord" or k == "HitBarycentric" then hitData[k] = v
			else hitData[k] = wrapObj(v) end
		end
		return hitData
	end

	--- Creates an acceleration struction (AccelStruct)
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param table? entities Sequential list of entities to build the acceleration structure from (or nil to create an empty structure)
	-- @return AccelStruct Built acceleration structure
	function vistrace_library.createAccel(entities)
		canRun()
		if entities then
			checkLuaType(entities, TYPE_TABLE)
			local unwrapped = {}
			for k, v in pairs(entities) do
				if debug_getmetatable(v) ~= entMetaTable then SF.ThrowTypeError("Entity", SF.GetType(v), 2, "Entity table entry not an entity.") end
				unwrapped[k] = uwrapEnt(v)
			end
			entities = unwrapped
		end
		return wrapAccel(vistrace.CreateAccel(entities))
	end
end

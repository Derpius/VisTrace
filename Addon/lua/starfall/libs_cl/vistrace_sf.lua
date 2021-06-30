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
--- Requires the binary module installed to use, which you can get here https://github.com/100PXSquared/VisTrace/releases
-- @name vistrace
-- @class library
-- @libtbl vistrace_library
SF.RegisterLibrary("vistrace")

return function(instance)
	local checkPermission = instance.player ~= SF.Superuser and SF.Permissions.check or function() end
	local vistrace_library = instance.Libraries.vistrace

	local uwrapEnt, uwrapVec, wrapObj = instance.Types.Entity.Unwrap, instance.Types.Vector.Unwrap, instance.WrapObject
	local entMetaTable, vecMetaTbl = instance.Types.Entity, instance.Types.Vector

	local function canRun()
		checkPermission(instance, nil, "vistrace")
		if not vistrace then SF.Throw("VisTrace binary module not installed (get it here https://github.com/100PXSquared/VisTrace/releases)", 3) end

		-- Verify the version of the addon and binary module are valid (given the addon, if loaded from workshop, is much more likely to update than the module which is manual)
		if not vistrace.ModuleVersion then SF.Throw("Legacy version of VisTrace binary module loaded, please update", 3) end
		if vistrace.ModuleVersion ~= vistrace.AddonVersion then
			SF.Throw("VisTrace addon and module versions do not match (" .. vistrace.AddonVersion .. "/" .. vistrace.ModuleVersion .. ")", 3)
		end
	end

	--- (Re)build the acceleration structure
	--- Note that only 1 of these is loaded into memory at a time and is used by every traversal, calling this will erase the previous state
	-- @param table? entities Sequential list of entities to build the acceleration structure from (or nil to clear the structure)
	function vistrace_library.rebuildAccel(entities)
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
		vistrace.RebuildAccel(entities)
	end

	--- Traverses the acceleration structure
	-- @param Vector origin Ray origin
	-- @param Vector direction Ray direction
	-- @param number? tMin Minimum distance of the ray (basically offset from start along direction)
	-- @param number? tMax Maximum distance of the ray
	-- @param boolean? hitWorld Enables calling util.TraceLine internally to hit the world (default: true)
	-- @return table Result of the traversal as a TraceResult struct with some extra values (see https://github.com/100PXSquared/VisTrace#usage)
	function vistrace_library.traverseScene(origin, direction, tMin, tMax, hitWorld)
		canRun()

		if debug_getmetatable(origin) ~= vecMetaTbl then SF.ThrowTypeError("Vector", SF.GetType(origin), 2) end
		validateVector(origin)

		if debug_getmetatable(direction) ~= vecMetaTbl then SF.ThrowTypeError("Vector", SF.GetType(direction), 2) end
		validateVector(direction)

		if tMin then checkLuaType(tMin, TYPE_NUMBER) end
		if tMax then checkLuaType(tMax, TYPE_NUMBER) end
		if hitWorld then checkLuaType(hitWorld, TYPE_BOOL) end

		local hitData = vistrace.TraverseScene(uwrapVec(origin), uwrapVec(direction), tMin, tMax, hitWorld)
		for k, v in pairs(hitData) do -- Note that vistrace returns tables, not actual TraceResult structs, so we can just enumerate and wrap rather than using SF.StructWrapper
			if k == "HitTexCoord" or k == "HitBarycentric" then hitData[k] = v
			else hitData[k] = wrapObj(v) end
		end
		return hitData
	end
end
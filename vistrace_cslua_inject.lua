--[[
	THIS SCRIPT IS DESIGNED TO LOAD THE BINARY MODULE
	AND PROVIDE AN EXTREMELY BASIC STARFALL INTEGRATION (AFTER AUTORUN) USING CSLUA,
	IN ENVIRONMENTS WHERE MOUNTING THE ADDON IS IMPOSSIBLE.

	IF YOU ARE ABLE TO MOUNT OR GET THE PROPER ADDON MOUNTED, THEN YOU SHOULD.
	THIS IS MERELY A BARE BONES ALTERNATIVE THAT CAN BE LOADED AFTER AUTORUN.

	Usage:
	Place this file in a location you can load lua from (most likely to be applicable is a filesystem addon folder's lua directory)
	Load into a game where the VisTrace addon is not mounted
	Run this script (if the game you're in has sv_allowcslua set to 1, then this as simple as lua_openscript_cl path/to/vistrace_cslua_inject.lua)

	If you attempt to run this script in an environment where the addon has already been mounted and loaded on your client,
	then it will throw an error to prevent overwriting the existing VisTrace addon.

	Of course you can disable these safeguards, but I don't recommend it.

	DO NOT PLACE THIS ANYWHERE IT WILL BE LOADED VIA AUTORUN, IF THE PROPER ADDON IS INSTALLED IT WILL CREATE CONFLICTS.
]]

if SERVER then return end
if vistrace then
	error("The VisTrace addon has already been mounted and loaded (or you've already injected), attempting to inject anyway will likely cause conflicts")
end

require("VisTrace-v0.4")

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
SF.Permissions.loadPermissionOptions()

SF.RegisterType("AccelStruct", true, false, debug.getregistry().AccelStruct)

SF.Modules.vistrace = {injected = {init = function(instance)
	instance.env.vistrace = {}

	local checkPermission = instance.player ~= SF.Superuser and SF.Permissions.check or function() end
	local vistrace_library = instance.env.vistrace

	local accelstruct_methods, accelstruct_meta = instance.Types.AccelStruct.Methods, instance.Types.AccelStruct
	local wrapAccel, uwrapAccel = instance.Types.AccelStruct.Wrap, instance.Types.AccelStruct.Unwrap

	local uwrapEnt, uwrapVec, wrapObj = instance.Types.Entity.Unwrap, instance.Types.Vector.Unwrap, instance.WrapObject
	local entMetaTable, vecMetaTbl = instance.Types.Entity, instance.Types.Vector

	local function canRun()
		checkPermission(instance, nil, "vistrace")
		if not vistrace then
			SF.Throw("The required version (v0.4.x) of the VisTrace binary module is not installed (get it here https://github.com/100PXSquared/VisTrace/releases)", 3)
		end
	end

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

		PrintTable(debug.getmetatable(uwrapAccel(self)))
		local hitData = uwrapAccel(self):Traverse(uwrapVec(origin), uwrapVec(direction), tMin, tMax, hitWorld, hitWater)
		for k, v in pairs(hitData) do -- Note that vistrace returns tables, not actual TraceResult structs, so we can just enumerate and wrap rather than using SF.StructWrapper
			if k == "HitTexCoord" or k == "HitBarycentric" then hitData[k] = v
			else hitData[k] = wrapObj(v) end
		end
		return hitData
	end

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
end}}

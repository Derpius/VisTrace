local inf = math.huge

local function validateVector(v)
	if (
		v[1] ~= v[1] or v[1] == inf or v[1] == -inf or
		v[2] ~= v[2] or v[2] == inf or v[2] == -inf or
		v[3] ~= v[3] or v[3] == inf or v[3] == -inf
	) then SF.Throw("Invalid vector, inf or NaN present", 3) end
end

local checkLuaType = SF.CheckLuaType
local debug_getmetatable = debug.getmetatable

SF.Permissions.registerPrivilege("vistrace.accel", "Create VisTrace AccelStructs", "Allows the user to build acceleration structures and traverse scenes", { client = {default = 1} })
SF.Permissions.registerPrivilege("vistrace.hdri", "Load VisTrace HDRI Samplers", "Allows the user to load HDRIs and sample them", { client = {default = 1} })

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

--- VisTrace random sampler
-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
-- @name Sampler
-- @class type
-- @libtbl sampler_methods
-- @libtbl sampler_meta
SF.RegisterType("Sampler", true, false, debug.getregistry().Sampler)

--- VisTrace HDRI sampler
-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
-- @name HDRI
-- @class type
-- @libtbl hdri_methods
-- @libtbl hdri_meta
SF.RegisterType("HDRI", true, false, debug.getregistry().HDRI)

return function(instance)
	local checkPermission = instance.player ~= SF.Superuser and SF.Permissions.check or function() end
	local vistrace_library = instance.Libraries.vistrace

	local accelstruct_methods, accelstruct_meta = instance.Types.AccelStruct.Methods, instance.Types.AccelStruct
	local wrapAccel, uwrapAccel = instance.Types.AccelStruct.Wrap, instance.Types.AccelStruct.Unwrap

	local sampler_methods, sampler_meta = instance.Types.Sampler.Methods, instance.Types.Sampler
	local wrapSampler, uwrapSampler = instance.Types.Sampler.Wrap, instance.Types.Sampler.Unwrap

	local hdri_methods, hdri_meta = instance.Types.HDRI.Methods, instance.Types.HDRI
	local wrapHDRI, uwrapHDRI = instance.Types.HDRI.Wrap, instance.Types.HDRI.Unwrap

	local uwrapEnt, uwrapVec, wrapVec, uwrapAng = instance.Types.Entity.Unwrap, instance.Types.Vector.Unwrap, instance.Types.Vector.Wrap, instance.Types.Angle.Unwrap
	local entMetaTable, vecMetaTbl, angleMetaTbl = instance.Types.Entity, instance.Types.Vector, instance.Types.Angle

	local uwrapObj, wrapObj = instance.UnwrapObject, instance.WrapObject

	local function checkVector(v)
		if debug_getmetatable(v) ~= vecMetaTbl then SF.ThrowTypeError("Vector", SF.GetType(v), 3) end
	end

	function accelstruct_meta.__tostring()
		return "AccelStruct"
	end

	function sampler_meta.__tostring()
		return "Sampler"
	end

	function hdri_meta.__tostring()
		return "HDRI"
	end

	local function canRun()
		if not vistrace then
			SF.Throw("The required version (v0.8.x) of the VisTrace binary module is not installed (get it here https://github.com/Derpius/VisTrace/releases)", 3)
		end
	end

	--- Rebuild the acceleration structure
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param table? entities Sequential list of entities to rebuild the acceleration structure with (or nil to clear the structure)
	function accelstruct_methods:rebuild(entities)
		checkPermission(instance, nil, "vistrace.accel")
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

		checkVector(origin)
		validateVector(origin)

		checkVector(direction)
		validateVector(direction)

		if tMin then checkLuaType(tMin, TYPE_NUMBER) end
		if tMax then checkLuaType(tMax, TYPE_NUMBER) end
		if hitWorld then checkLuaType(hitWorld, TYPE_BOOL) end
		if hitWater then checkLuaType(hitWater, TYPE_BOOL) end

		local hitData = uwrapAccel(self):Traverse(uwrapVec(origin), uwrapVec(direction), tMin, tMax, hitWorld, hitWater)
		for k, v in pairs(hitData) do -- Note that vistrace returns tables, not actual TraceResult structs, so we can just enumerate and wrap rather than using SF.StructWrapper
			if k == "HitTexCoord" or k == "HitBarycentric" then
				hitData[k] = v
			elseif k == "HitShader" then
				hitData[k] = {Albedo = wrapObj(v.Albedo), Alpha = v.Alpha, Roughness = v.Roughness, Material = wrapObj(v.Material)}
			else hitData[k] = wrapObj(v) end
		end
		return hitData
	end

	--- Creates an acceleration struction (AccelStruct)
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param table? entities Sequential list of entities to build the acceleration structure from (or nil to create an empty structure)
	-- @return AccelStruct Built acceleration structure
	function vistrace_library.createAccel(entities)
		checkPermission(instance, nil, "vistrace.accel")
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

	--- Gets a uniform random float from the sampler
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number Random float in a 0-1 range
	function sampler_methods:getFloat()
		canRun()
		return uwrapSampler(self):GetFloat()
	end

	--- Gets two uniform random floats from the sampler
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number Random float in a 0-1 range
	-- @return number Random float in a 0-1 range
	function sampler_methods:getFloat2D()
		canRun()
		return uwrapSampler(self):GetFloat2D()
	end

	--- Creates a random sampler
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number? seed uint32_t to seed the sampler with
	-- @return Sampler Sampler object
	function vistrace_library.createSampler(seed)
		canRun()

		if seed ~= nil then checkLuaType(seed, TYPE_NUMBER) end
		return wrapSampler(vistrace.CreateSampler(seed))
	end

	--- BSDF Lobes
	-- @name vistrace_library.LobeType
	-- @class table
	-- @field None
	-- @field DiffuseReflection
	-- @field SpecularReflection
	-- @field DeltaReflection
	-- @field DiffuseTransmission
	-- @field SpecularTransmission
	-- @field DeltaTransmission
	-- @field Diffuse
	-- @field Specular
	-- @field Delta
	-- @field NonDelta
	-- @field Reflection
	-- @field Transmission
	-- @field NonDeltaReflection
	-- @field NonDeltaTransmission
	-- @field All
	instance.env.LobeType = {
		None = 0x00,

		DiffuseReflection = 0x01,
		SpecularReflection = 0x02,
		DeltaReflection = 0x04,

		DiffuseTransmission = 0x10,
		SpecularTransmission = 0x20,
		DeltaTransmission = 0x40,

		Diffuse = 0x11,
		Specular = 0x22,
		Delta = 0x44,
		NonDelta = 0x33,

		Reflection = 0x0f,
		Transmission = 0xf0,

		NonDeltaReflection = 0x03,
		NonDeltaTransmission = 0x30,

		All = 0xff,
	}

	--- Importance samples the Falcor BSDF
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param Sampler sampler Sampler object
	-- @param table material Material parameters (See the GitHub for valid params)
	-- @param Vector normal Surface normal
	-- @param Vector tangent Surface tangent
	-- @param Vector binormal Surface binormal
	-- @param Vector wo Outgoing light direction (towards the camera)
	-- @param boolean? thin Whether to simulate the material as a thin film
	-- @return bool valid Whether the sample is valid or not
	-- @return table? sample Sample generated (if valid)
	function vistrace_library.sampleBSDF(sampler, material, normal, tangent, binormal, wo, thin)
		canRun()

		if debug_getmetatable(sampler) ~= sampler_meta then SF.ThrowTypeError("Sampler", SF.GetType(sampler), 2) end
		checkLuaType(material, TYPE_TABLE)

		checkVector(normal)
		validateVector(normal)

		checkVector(tangent)
		validateVector(tangent)

		checkVector(binormal)
		validateVector(binormal)

		checkVector(wo)
		validateVector(wo)

		if thin ~= nil then checkLuaType(thin, TYPE_BOOL) end

		local unwrappedMat = {}
		for k, v in pairs(material) do
			unwrappedMat[k] = uwrapObj(v)
		end

		local valid, sample = vistrace.SampleBSDF(
			uwrapSampler(sampler), unwrappedMat,
			uwrapVec(normal), uwrapVec(tangent), uwrapVec(binormal),
			uwrapVec(wo),
			thin
		)

		if not valid then return false end

		return true, {
			wo = wrapVec(sample.wo),
			pdf = sample.pdf,
			weight = wrapVec(sample.weight),
			lobe = sample.lobe
		}
	end

	--- Evaluates the Falcor BSDF
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param table material Material parameters (See the GitHub for valid params)
	-- @param Vector normal Surface normal
	-- @param Vector tangent Surface tangent
	-- @param Vector binormal Surface binormal
	-- @param Vector wo Outgoing light direction (towards the camera)
	-- @param Vector wi Incoming light direction (towards sampled direction or light)
	-- @param boolean? thin Whether to simulate the material as a thin film
	-- @return Vector Evaluated surface colour
	function vistrace_library.evalBSDF(material, normal, tangent, binormal, wo, wi, thin)
		canRun()

		checkLuaType(material, TYPE_TABLE)

		checkVector(normal)
		validateVector(normal)

		checkVector(tangent)
		validateVector(tangent)

		checkVector(binormal)
		validateVector(binormal)

		checkVector(wo)
		validateVector(wo)

		checkVector(wi)
		validateVector(wi)

		if thin ~= nil then checkLuaType(thin, TYPE_BOOL) end

		local unwrappedMat = {}
		for k, v in pairs(material) do
			unwrappedMat[k] = uwrapObj(v)
		end

		return wrapVec(vistrace.EvalBSDF(
			unwrappedMat,
			uwrapVec(normal), uwrapVec(tangent), uwrapVec(binormal),
			uwrapVec(wo), uwrapVec(wi),
			thin
		))
	end

	--- Evaluates the Falcor BSDF's PDF
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param table material Material parameters (See the GitHub for valid params)
	-- @param Vector normal Surface normal
	-- @param Vector tangent Surface tangent
	-- @param Vector binormal Surface binormal
	-- @param Vector wo Outgoing light direction (towards the camera)
	-- @param Vector wi Incoming light direction (towards sampled direction or light)
	-- @param boolean? thin Whether to simulate the material as a thin film
	-- @return number Evaluated PDF
	function vistrace_library.evalPDF(material, normal, tangent, binormal, wo, wi, thin)
		canRun()

		checkLuaType(material, TYPE_TABLE)

		checkVector(normal)
		validateVector(normal)

		checkVector(tangent)
		validateVector(tangent)

		checkVector(binormal)
		validateVector(binormal)

		checkVector(wo)
		validateVector(wo)

		checkVector(wi)
		validateVector(wi)

		if thin ~= nil then checkLuaType(thin, TYPE_BOOL) end

		local unwrappedMat = {}
		for k, v in pairs(material) do
			unwrappedMat[k] = uwrapObj(v)
		end

		return vistrace.EvalPDF(
			unwrappedMat,
			uwrapVec(normal), uwrapVec(tangent), uwrapVec(binormal),
			uwrapVec(wo), uwrapVec(wi),
			thin
		)
	end

	--- Loads a HDRI from `garrysmod/data/vistrace_hdris/` and appends the `.hdr` extension automatically  
	--- Subfolders are allowed
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param string path Path to the HDRI
	-- @param number? radianceThreshold Minimum total radiance in a sample bin before the bin is no longer divided
	-- @param number? areaThreshold Minimum area (in pixels) of a sample bin before the bin is no longer divided
	-- @return HDRI HDRI sampler
	function vistrace_library.loadHDRI(path, radianceThreshold, areaThreshold)
		checkPermission(instance, nil, "vistrace.hdri")
		canRun()

		if path ~= nil then checkLuaType(path, TYPE_STRING) end
		if radianceThreshold ~= nil then checkLuaType(radianceThreshold, TYPE_NUMBER) end
		if areaThreshold ~= nil then checkLuaType(areaThreshold, TYPE_NUMBER) end

		return wrapHDRI(vistrace.LoadHDRI(path, radianceThreshold, areaThreshold))
	end

	--- Checks if the HDRI is valid
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return boolean Whether the HDRI is valid
	function hdri_methods:isValid()
		canRun()
		return uwrapHDRI(self):IsValid()
	end

	--- Samples a pixel from the HDRI
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param Vector direction Direction to get the colour of
	-- @return Vector Colour value
	-- @return number Radiance
	function hdri_methods:getPixel(direction)
		canRun()
		checkVector(direction)
		validateVector(direction)

		local colour, radiance = uwrapHDRI(self):GetPixel(uwrapVec(direction))
		return wrapVec(colour), radiance
	end

	--- Calculates the probability of sampling this direction
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param Vector direction Direction to get the probability of
	-- @return number Probability
	function hdri_methods:evalPDF(direction)
		canRun()
		checkVector(direction)
		validateVector(direction)

		return uwrapHDRI(self):EvalPDF(uwrapVec(direction))
	end

	--- Importance samples the HDRI (only the first value is returned if the sample is invalid)
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param Sampler
	-- @return boolean Whether the sample is valid
	-- @return Vector? Sampled direction
	-- @return Vector? Sampled colour
	-- @return number? Evaluated PDF
	function hdri_methods:sample(sampler)
		canRun()
		if debug_getmetatable(sampler) ~= sampler_meta then SF.ThrowTypeError("Sampler", SF.GetType(sampler), 2) end
		
		local valid, dir, colour, pdf = uwrapHDRI(self):Sample(uwrapSampler(sampler))

		if not valid then return false end
		return true, wrapVec(dir), wrapVec(colour), pdf
	end

	--- Sets the rotation of the HDRI
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param Angle angle Angle to set
	function hdri_methods:setAngles(angle)
		canRun()
		if debug_getmetatable(angle) ~= angleMetaTbl then SF.ThrowTypeError("Angle", SF.GetType(angle), 2) end
		uwrapHDRI(self):SetAngles(uwrapAng(angle))
	end

	--- Calculates a biased offset from an intersection point to prevent self intersection
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param Vector origin
	-- @param Vector normal
	-- @return Vector New ray origin
	function vistrace_library.calcRayOrigin(origin, normal)
		canRun()

		checkVector(origin)
		validateVector(origin)

		checkVector(normal)
		validateVector(normal)

		return wrapVec(vistrace.CalcRayOrigin(uwrapVec(origin), uwrapVec(normal)))
	end
end

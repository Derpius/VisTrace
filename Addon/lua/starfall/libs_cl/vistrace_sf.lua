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

SF.Permissions.registerPrivilege("vistrace.rendertarget", "Create VisTrace Render Targets", "Allows the user to create render targets", { client = {default = 1} })
SF.Permissions.registerPrivilege("vistrace.accel", "Create VisTrace AccelStructs", "Allows the user to build acceleration structures and traverse scenes", { client = {default = 1} })
SF.Permissions.registerPrivilege("vistrace.hdri", "Load VisTrace HDRI Samplers", "Allows the user to load HDRIs and sample them", { client = {default = 1} })

--- Constructs and traverses a BVH acceleration structure on the CPU allowing for high speed vismesh intersections
--- Requires the binary module installed to use, which you can get here https://github.com/Derpius/VisTrace/releases
-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
-- @name vistrace
-- @class library
-- @libtbl vistrace_library
SF.RegisterLibrary("vistrace")

--- VisTrace render target object returned by vistrace.createRenderTarget
-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
-- @name VisTraceRT
-- @class type
-- @libtbl vistracert_methods
-- @libtbl vistracert_meta
SF.RegisterType("VisTraceRT", true, false, debug.getregistry().VisTraceRT)

--- VisTrace TraceResult object returned by AccelStruct:traverse
-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
-- @name VisTraceResult
-- @class type
-- @libtbl traceresult_methods
-- @libtbl traceresult_meta
SF.RegisterType("VisTraceResult", true, false, debug.getregistry().VisTraceResult)

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

--- VisTrace BSDF material
-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
-- @name BSDFMaterial
-- @class type
-- @libtbl bsdfmaterial_methods
-- @libtbl bsdfmaterial_meta
SF.RegisterType("BSDFMaterial", true, false, debug.getregistry().BSDFMaterial)

return function(instance)
	local checkPermission = instance.player ~= SF.Superuser and SF.Permissions.check or function() end
	local vistrace_library = instance.Libraries.vistrace

	local vistracert_methods, vistracert_meta = instance.Types.VisTraceRT.Methods, instance.Types.VisTraceRT
	local wrapRT, uwrapRT = instance.Types.VisTraceRT.Wrap, instance.Types.VisTraceRT.Unwrap

	local accelstruct_methods, accelstruct_meta = instance.Types.AccelStruct.Methods, instance.Types.AccelStruct
	local wrapAccel, uwrapAccel = instance.Types.AccelStruct.Wrap, instance.Types.AccelStruct.Unwrap

	local traceresult_methods, traceresult_meta = instance.Types.VisTraceResult.Methods, instance.Types.VisTraceResult
	local wrapResult, uwrapResult = instance.Types.VisTraceResult.Wrap, instance.Types.VisTraceResult.Unwrap

	local sampler_methods, sampler_meta = instance.Types.Sampler.Methods, instance.Types.Sampler
	local wrapSampler, uwrapSampler = instance.Types.Sampler.Wrap, instance.Types.Sampler.Unwrap

	local hdri_methods, hdri_meta = instance.Types.HDRI.Methods, instance.Types.HDRI
	local wrapHDRI, uwrapHDRI = instance.Types.HDRI.Wrap, instance.Types.HDRI.Unwrap

	local bsdfmaterial_methods, bsdfmaterial_meta = instance.Types.BSDFMaterial.Methods, instance.Types.BSDFMaterial
	local wrapMat, uwrapMat = instance.Types.BSDFMaterial.Wrap, instance.Types.BSDFMaterial.Unwrap

	local uwrapVec, wrapVec, uwrapAng = instance.Types.Vector.Unwrap, instance.Types.Vector.Wrap, instance.Types.Angle.Unwrap
	local wrapEnt, uwrapEnt = instance.Types.Entity.Wrap, instance.Types.Entity.Unwrap
	local entMetaTable, vecMetaTbl, angleMetaTbl = instance.Types.Entity, instance.Types.Vector, instance.Types.Angle

	local uwrapObj, wrapObj = instance.UnwrapObject, instance.WrapObject

	local function checkVector(v)
		if debug_getmetatable(v) ~= vecMetaTbl then SF.ThrowTypeError("Vector", SF.GetType(v), 3) end
	end

	function vistracert_meta.__tostring()
		return "VisTraceRT"
	end

	function accelstruct_meta.__tostring()
		return "AccelStruct"
	end

	function traceresult_meta.__tostring()
		return "VisTraceResult"
	end

	function sampler_meta.__tostring()
		return "Sampler"
	end

	function hdri_meta.__tostring()
		return "HDRI"
	end

	function bsdfmaterial_meta.__tostring()
		return "BSDFMaterial"
	end

	local function canRun()
		if not vistrace then
			SF.Throw("The required version (v" .. VISTRACE_VERSION .. ") of the VisTrace binary module is not installed (get it here https://github.com/Derpius/VisTrace/releases)", 3)
		end
	end

--#region VisTraceRT

	--- Render target image formats
	-- @name vistrace_library.VisTraceRTFormat
	-- @class table
	-- @field R8
	-- @field RG88
	-- @field RGB888
	-- @field RGBFFF
	-- @field Albedo
	-- @field Normal
	instance.env.VisTraceRTFormat = VisTraceRTFormat

	--- Creates a render target designed for high performance image processing
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number width
	-- @param number height
	-- @param VisTraceRTFormat format Image format to use for the underlying pixel data
	-- @return VisTraceRT
	function vistrace_library.createRenderTarget(width, height, format)
		checkPermission(instance, nil, "vistrace.rendertarget")
		canRun()

		checkLuaType(width, TYPE_NUMBER)
		checkLuaType(height, TYPE_NUMBER)
		checkLuaType(format, TYPE_NUMBER)

		return wrapRT(vistrace.CreateRenderTarget(width, height, format))
	end

	--- Returns true if the render target is valid, false otherwise
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return boolean
	function vistracert_methods:isValid()
		canRun()
		return uwrapRT(self):IsValid()
	end

	--- Resizes the render target and returns true if the resize was successful
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number width New width
	-- @param number height New height
	-- @return boolean
	function vistracert_methods:resize(width, height)
		canRun()
		return uwrapRT(self):Resize(width, height)
	end

	--- Gets the width of the render target in pixels
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number
	function vistracert_methods:getWidth()
		canRun()
		return uwrapRT(self):GetWidth()
	end

	--- Gets the height of the render target in pixels
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number
	function vistracert_methods:getHeight()
		canRun()
		return uwrapRT(self):GetHeight()
	end

	--- Gets the format of the render target
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return VisTraceRTFormat
	function vistracert_methods:getFormat()
		canRun()
		return uwrapRT(self):GetFormat()
	end

	--- Gets the colour values of each channel of the pixel, normalised to 0-1
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number x X coordinate of the pixel
	-- @param number y Y coordinate of the pixel
	-- @return ...number Values of each channel
	function vistracert_methods:getPixel(x, y)
		canRun()
		return uwrapRT(self):GetPixel(x, y)
	end

	--- Sets the colour values of each channel of the pixel, normalised to 0-1
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number x X coordinate of the pixel
	-- @param number y Y coordinate of the pixel
	-- @param ...number Values of each channel
	function vistracert_methods:setPixel(x, y, ...)
		canRun()
		return uwrapRT(self):SetPixel(x, y, ...)
	end

	--- Tonemaps a HDR render target using ACES fitted  
	--- Apply exposure before calling this and gamma correction after
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	function vistracert_methods:tonemap()
		canRun()
		return uwrapRT(self):Tonemap()
	end

--#endregion

--#region VisTraceResult

	--- Gets the hit pos of the result
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return Vector Position of the point of intersection
	function traceresult_methods:pos()
		canRun()
		return wrapVec(uwrapResult(self):Pos())
	end

	--- Gets the distance from the ray origin to the hit pos of the result  
	--- This is extremely fast compared to computing the distance between origin and pos yourself, as it's calculated during triangle intersection
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number Distance to the point of intersection
	function traceresult_methods:distance()
		canRun()
		return uwrapResult(self):Distance()
	end

	--- Gets the entity the ray hit
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return Entity Entity that was hit
	function traceresult_methods:entity()
		canRun()
		return wrapEnt(uwrapResult(self):Entity())
	end

	--- Gets the geometric normal of the tri that was hit
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return Vector Geometric normal
	function traceresult_methods:geometricNormal()
		canRun()
		return wrapVec(uwrapResult(self):GeometricNormal())
	end
	--- Gets the shading normal of the intersection
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return Vector Shading normal after weighting and normal mapping
	function traceresult_methods:normal()
		canRun()
		return wrapVec(uwrapResult(self):Normal())
	end
	--- Gets the shading tangent of the intersection
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return Vector Shading tangent after weighting and normal mapping
	function traceresult_methods:tangent()
		canRun()
		return wrapVec(uwrapResult(self):Tangent())
	end
	--- Gets the shading binormal of the intersection
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return Vector Shading binormal after weighting and normal mapping
	function traceresult_methods:binormal()
		canRun()
		return wrapVec(uwrapResult(self):Binormal())
	end

	--- Gets the barycentric coordinates of the intersection
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return Vector Vector containing the UVW of the intersection mapped to XYZ
	function traceresult_methods:barycentric()
		canRun()
		return wrapVec(uwrapResult(self):Barycentric())
	end

	--- Gets the texture UV of the intersection
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return table Table with keys u, v
	function traceresult_methods:textureUV()
		canRun()
		return uwrapResult(self):TextureUV()
	end

	--- Gets the submaterial index of the hit tri
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number Submat index
	function traceresult_methods:subMaterialIndex()
		canRun()
		return uwrapResult(self):SubMaterialIndex()
	end

	--- Gets the albedo of the intersection after applying entity colour and base texture
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return Vector Colour normalised to 0-1
	function traceresult_methods:albedo()
		canRun()
		return wrapVec(uwrapResult(self):Albedo())
	end
	--- Gets the alpha of the intersection after applying entity colour and base texture
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number Alpha normalised to 0-1
	function traceresult_methods:alpha()
		canRun()
		return uwrapResult(self):Alpha()
	end
	--- Gets the metalness of the intersection after applying MRAO texture
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number Metalness normailsed to 0-1 (defaults to 0 if no MRAO found)
	function traceresult_methods:metalness()
		canRun()
		return uwrapResult(self):Metalness()
	end
	--- Gets the roughness of the intersection after applying MRAO texture
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number roughness normailsed to 0-1 (defaults to 1 if no MRAO found)
	function traceresult_methods:roughness()
		canRun()
		return uwrapResult(self):Roughness()
	end

	--- Gets the material's $flags value
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number Bitflags
	function traceresult_methods:materialFlags()
		canRun()
		return uwrapResult(self):MaterialFlags()
	end
	--- Gets the material's SURF flags (only present on world)
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number Bitflags
	function traceresult_methods:surfaceFlags()
		canRun()
		return uwrapResult(self):SurfaceFlags()
	end

	--- Gets whether we hit the sky
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return boolean True if we hit the sky of the map
	function traceresult_methods:hitSky()
		canRun()
		return uwrapResult(self):HitSky()
	end

	--- Gets whether we hit water
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return boolean True if we hit a tri using a water shader
	function traceresult_methods:hitWater()
		canRun()
		return uwrapResult(self):HitWater()
	end

	--- Gets whether we hit the front side of a tri
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return boolean True if we hit the front face of a tri
	function traceresult_methods:frontFacing()
		canRun()
		return uwrapResult(self):FrontFacing()
	end

	--- Gets the floating point MIP level that was sampled from the base texture  
	--- Mainly used for debugging texture LoD calculation
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number Floating point MIP level (where 0 is the highest resolution)
	function traceresult_methods:baseMIPLevel()
		canRun()
		return uwrapResult(self):BaseMIPLevel()
	end

--#endregion

--#region AccelStruct

	--- Rebuild the acceleration structure
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param table? entities Sequential list of entities to rebuild the acceleration structure with (or nil to clear the structure)
	-- @param boolean? traceWorld Whether to include the world in the acceleration structure (defaults to true)
	function accelstruct_methods:rebuild(entities, traceWorld)
		checkPermission(instance, nil, "vistrace.accel")
		canRun()
		
		if traceWorld ~= nil then checkLuaType(traceWorld, TYPE_BOOL) end

		if entities then
			checkLuaType(entities, TYPE_TABLE)
			local unwrapped = {}
			for k, v in pairs(entities) do
				if debug_getmetatable(v) ~= entMetaTable then SF.ThrowTypeError("Entity", SF.GetType(v), 2, "Entity table entry not an entity.") end
				unwrapped[k] = uwrapEnt(v)
			end
			entities = unwrapped
		end
		uwrapAccel(self):Rebuild(entities, traceWorld)
	end

	--- Traverses the acceleration structure
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param Vector origin Ray origin
	-- @param Vector direction Ray direction
	-- @param number? tMin Minimum distance of the ray (basically offset from start along direction)
	-- @param number? tMax Maximum distance of the ray
	-- @param boolean? hitWorld Enables calling util.TraceLine internally to hit the world (default: true)
	-- @param boolean? hitWater Enables calling util.TraceLine internally to hit water (default: false)
	-- @return VisTraceResult? Result of the traversal, or nil if ray missed
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

		local res = uwrapAccel(self):Traverse(uwrapVec(origin), uwrapVec(direction), tMin, tMax, hitWorld, hitWater)
		if res then
			return wrapResult(res)
		end
	end

	--- Creates an acceleration struction (AccelStruct)
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param table? entities Sequential list of entities to build the acceleration structure from (or nil to create an empty structure)
	-- @param boolean? traceWorld Whether to include the world in the acceleration structure (defaults to true)
	-- @return AccelStruct Built acceleration structure
	function vistrace_library.createAccel(entities, traceWorld)
		checkPermission(instance, nil, "vistrace.accel")
		canRun()

		if traceWorld ~= nil then checkLuaType(traceWorld, TYPE_BOOL) end

		if entities then
			checkLuaType(entities, TYPE_TABLE)
			local unwrapped = {}
			for k, v in pairs(entities) do
				if debug_getmetatable(v) ~= entMetaTable then SF.ThrowTypeError("Entity", SF.GetType(v), 2, "Entity table entry not an entity.") end
				unwrapped[k] = uwrapEnt(v)
			end
			entities = unwrapped
		end
		return wrapAccel(vistrace.CreateAccel(entities, traceWorld))
	end

--#endregion

--#region Sampler

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

--#endregion

--#region BSDFMaterial

	--- Creates a new material for use with BSDF sampling
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return BSDFMaterial New material object
	function vistrace_library.createMaterial()
		canRun()
		return wrapMat(vistrace.CreateMaterial())
	end

	--- Set the colour of the material (multiplies by the sampled colour)
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param Vector colour Colour to set as a 0-1 normalised vector
	function bsdfmaterial_methods:colour(colour)
		canRun()
		uwrapMat(self):Colour(uwrapVec(colour))
	end

	--- Set the metalness of the material (overrides PBR textures)
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number metalness Metalness to set
	function bsdfmaterial_methods:metalness(metalness)
		canRun()
		checkLuaType(metalness, TYPE_NUMBER)
		uwrapMat(self):Metalness(metalness)
	end
	--- Set the roughness of the material (overrides PBR textures)
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number roughness Roughness to set
	function bsdfmaterial_methods:roughness(roughness)
		canRun()
		checkLuaType(roughness, TYPE_NUMBER)
		uwrapMat(self):Roughness(roughness)
	end

	--- Set the index of refraction of the material
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number ior Index of refraction to set
	function bsdfmaterial_methods:ior(ior)
		canRun()
		checkLuaType(ior, TYPE_NUMBER)
		uwrapMat(self):IoR(ior)
	end
	--- Set the relative index of refraction of the material
	--- Used to implement nested dielectrics (dont use if you dont know what you're doing)
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number relativeIor Relative index of refraction to set
	function bsdfmaterial_methods:relativeIor(relativeIor)
		canRun()
		checkLuaType(relativeIor, TYPE_NUMBER)
		uwrapMat(self):RelativeIoR(relativeIor)
	end

	--- Set the diffuse transmission amount
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number diffuseTransmission 0-1 where 0 is no light transmitted and 1 is all light transmitted
	function bsdfmaterial_methods:diffuseTransmission(diffuseTransmission)
		canRun()
		checkLuaType(diffuseTransmission, TYPE_NUMBER)
		uwrapMat(self):DiffuseTransmission(diffuseTransmission)
	end
	--- Set the specular transmission amount
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number specularTransmission 0-1 where 0 is no light refracted and 1 is all light refracted
	function bsdfmaterial_methods:specularTransmission(specularTransmission)
		canRun()
		checkLuaType(specularTransmission, TYPE_NUMBER)
		uwrapMat(self):SpecularTransmission(specularTransmission)
	end

	--- Toggle thin film
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param boolean thin True to simulate the material as thin film
	function bsdfmaterial_methods:thin(thin)
		canRun()
		checkLuaType(thin, TYPE_BOOL)
		uwrapMat(self):Thin(thin)
	end

--#endregion

--#region BSDF

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
	-- @param BSDFMaterial material Material parameters
	-- @return bool valid Whether the sample is valid or not
	-- @return table? sample Sample generated (if valid)
	function traceresult_methods:sampleBSDF(sampler, material)
		canRun()

		if debug_getmetatable(sampler) ~= sampler_meta then SF.ThrowTypeError("Sampler", SF.GetType(sampler), 2) end
		if debug_getmetatable(material) ~= bsdfmaterial_meta then SF.ThrowTypeError("BSDFMaterial", SF.GetType(material), 2) end

		local valid, sample = uwrapResult(self):SampleBSDF(uwrapSampler(sampler), uwrapMat(material))
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
	-- @param BSDFMaterial material Material parameters
	-- @param Vector wi Incoming light direction (towards sampled direction or light)
	-- @return Vector Evaluated surface colour
	function traceresult_methods:evalBSDF(material, wi)
		canRun()

		if debug_getmetatable(material) ~= bsdfmaterial_meta then SF.ThrowTypeError("BSDFMaterial", SF.GetType(material), 2) end

		checkVector(wi)
		validateVector(wi)

		return wrapVec(uwrapResult(self):EvalBSDF(uwrapMat(material), uwrapVec(wi)))
	end

	--- Evaluates the Falcor BSDF's PDF
	-- @src https://github.com/Derpius/VisTrace/blob/master/Addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param BSDFMaterial material Material parameters
	-- @param Vector wi Incoming light direction (towards sampled direction or light)
	-- @return number Evaluated PDF
	function traceresult_methods:evalPDF(material, wi)
		canRun()

		if debug_getmetatable(material) ~= bsdfmaterial_meta then SF.ThrowTypeError("BSDFMaterial", SF.GetType(material), 2) end

		checkVector(wi)
		validateVector(wi)

		return uwrapResult(self):EvalPDF(uwrapMat(material), uwrapVec(wi))
	end

--#endregion

--#region HDRI

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

--#endregion

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

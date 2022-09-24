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
-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
-- @name vistrace
-- @class library
-- @libtbl vistrace_library
SF.RegisterLibrary("vistrace")

--- VisTrace VTF texture interface for reading and sampling textures
-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
-- @name VisTraceVTFTexture
-- @class type
-- @libtbl vistracevtf_methods
-- @libtbl vistracevtf_meta
SF.RegisterType("VisTraceVTFTexture", true, false, debug.getregistry().VisTraceVTFTexture)

--- VisTrace render target object returned by vistrace.createRenderTarget
-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
-- @name VisTraceRT
-- @class type
-- @libtbl vistracert_methods
-- @libtbl vistracert_meta
SF.RegisterType("VisTraceRT", true, false, debug.getregistry().VisTraceRT)

--- VisTrace TraceResult object returned by AccelStruct:traverse
-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
-- @name VisTraceResult
-- @class type
-- @libtbl traceresult_methods
-- @libtbl traceresult_meta
SF.RegisterType("VisTraceResult", true, false, debug.getregistry().VisTraceResult)

--- VisTrace acceleration structure
-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
-- @name AccelStruct
-- @class type
-- @libtbl accelstruct_methods
-- @libtbl accelstruct_meta
SF.RegisterType("AccelStruct", true, false, debug.getregistry().AccelStruct)

--- VisTrace random sampler
-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
-- @name Sampler
-- @class type
-- @libtbl sampler_methods
-- @libtbl sampler_meta
SF.RegisterType("Sampler", true, false, debug.getregistry().Sampler)

--- VisTrace HDRI sampler
-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
-- @name HDRI
-- @class type
-- @libtbl hdri_methods
-- @libtbl hdri_meta
SF.RegisterType("HDRI", true, false, debug.getregistry().HDRI)

--- VisTrace BSDF material  
--- Pass no arguments to any functions to return the values stored in the material
-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
-- @name BSDFMaterial
-- @class type
-- @libtbl bsdfmaterial_methods
-- @libtbl bsdfmaterial_meta
SF.RegisterType("BSDFMaterial", true, false, debug.getregistry().BSDFMaterial)

return function(instance)
	local checkPermission = instance.player ~= SF.Superuser and SF.Permissions.check or function() end
	local vistrace_library = instance.Libraries.vistrace

	local vistracevtf_methods, vistracevtf_meta = instance.Types.VisTraceVTFTexture.Methods, instance.Types.VisTraceVTFTexture
	local wrapVTF, uwrapVTF = instance.Types.VisTraceVTFTexture.Wrap, instance.Types.VisTraceVTFTexture.Unwrap

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

	local ents_methods, ent_meta = instance.Types.Entity.Methods, instance.Types.Entity
	local wrapEnt, uwrapEnt = instance.Types.Entity.Wrap, instance.Types.Entity.Unwrap

	local vecMetaTbl, angleMetaTbl = instance.Types.Vector, instance.Types.Angle
	local uwrapVec, wrapVec, uwrapAng = instance.Types.Vector.Unwrap, instance.Types.Vector.Wrap, instance.Types.Angle.Unwrap

	local uwrapObj, wrapObj = instance.UnwrapObject, instance.WrapObject

	local function checkVector(v)
		if debug_getmetatable(v) ~= vecMetaTbl then SF.ThrowTypeError("Vector", SF.GetType(v), 3) end
	end

	function vistracevtf_meta.__tostring()
		return "VisTraceVTFTexture"
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

--#region VisTraceVTFTexture

	--- Loads a VTF texture
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param string path Path to the VTF texture relative to the materials folder and without an extension
	-- @return VisTraceVTFTexture
	function vistrace_library.loadTexture(path)
		canRun()
		checkLuaType(path, TYPE_STRING)
		return wrapVTF(vistrace.LoadTexture(path))
	end

	--- Returns true if the VTF texture is valid, false otherwise
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return boolean
	function vistracevtf_methods:isValid()
		canRun()
		return uwrapVTF(self):IsValid()
	end

	--- Gets the width of the specified MIP level
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number? mip Mip level to get the width of (Defaults to 0)
	-- @return number
	function vistracevtf_methods:getWidth(mip)
		canRun()

		if mip ~= nil then checkLuaType(mip, TYPE_NUMBER) end
		return uwrapVTF(self):GetWidth(mip)
	end

	--- Gets the height of the specified MIP level
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number? mip Mip level to get the height of (Defaults to 0)
	-- @return number
	function vistracevtf_methods:getHeight(mip)
		canRun()

		if mip ~= nil then checkLuaType(mip, TYPE_NUMBER) end
		return uwrapVTF(self):GetHeight(mip)
	end

	--- Gets the depth of the specified MIP level (volumetric textures only)
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number? mip Mip level to get the depth of (Defaults to 0)
	-- @return number
	function vistracevtf_methods:getDepth(mip)
		canRun()

		if mip ~= nil then checkLuaType(mip, TYPE_NUMBER) end
		return uwrapVTF(self):GetDepth(mip)
	end

	--- Gets the number offaces in this texture
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number
	function vistracevtf_methods:getFaces()
		canRun()
		return uwrapVTF(self):GetFaces()
	end

	--- Gets the number of MIP levels in this texture
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number
	function vistracevtf_methods:getMIPLevels()
		canRun()
		return uwrapVTF(self):GetMIPLevels()
	end

	--- Gets the number of frames in this texture (animated textures only)
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number
	function vistracevtf_methods:getFrames()
		canRun()
		return uwrapVTF(self):GetFrames()
	end

	--- Gets the first frame of animation
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number
	function vistracevtf_methods:getFirstFrame()
		canRun()
		return uwrapVTF(self):GetFirstFrame()
	end

	--- Gets a pixel from the texture given a set of exact coordinates
	--- In most cases you'll want to use VisTraceVTFTexture:sample()
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number x X coordinate of the pixel
	-- @param number y Y coordinate of the pixel
	-- @param number? z Z coordinate of the pixel (Defaults to 0)
	-- @param number? mip MIP level to read from (Defaults to 0). This does not transform your X and Y coordinates automatically
	-- @param number? frame Frame of animation to read from (Defaults to 0)
	-- @param number? face Environment map face to read from (Defaults to 0)
	-- @return Vector RGB components of the pixel
	-- @return number Alpha component of the pixel
	function vistracevtf_methods:getPixel(x, y, z, mip, frame, face)
		canRun()

		checkLuaType(x, TYPE_NUMBER)
		checkLuaType(y, TYPE_NUMBER)
		checkLuaType(z, TYPE_NUMBER)
		if mip ~= nil then checkLuaType(mip, TYPE_NUMBER) end
		if frame ~= nil then checkLuaType(frame, TYPE_NUMBER) end
		if face ~= nil then checkLuaType(face, TYPE_NUMBER) end

		local rgb, a = uwrapVTF(self):GetPixel(x, y, z, mip, frame, face)
		return wrapVec(rgb), a
	end

	--- Samples a pixel with filtering
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number u U coordinate to sample (0-1)
	-- @param number v V coordinate to sample (0-1)
	-- @param number? mip MIP level to read from (Defaults to 0). This can be a number between mip levels for trilinear filtering
	-- @param number? frame Frame of animation to read from (Defaults to 0)
	-- @param number? face Environment map face to read from (Defaults to 0)
	-- @return Vector RGB components of the pixel
	-- @return number Alpha component of the pixel
	function vistracevtf_methods:sample(u, v, mip, frame, face)
		canRun()

		checkLuaType(u, TYPE_NUMBER)
		checkLuaType(v, TYPE_NUMBER)
		if mip ~= nil then checkLuaType(mip, TYPE_NUMBER) end
		if frame ~= nil then checkLuaType(frame, TYPE_NUMBER) end
		if face ~= nil then checkLuaType(face, TYPE_NUMBER) end

		local rgb, a = uwrapVTF(self):Sample(u, v, mip, frame, face)
		return wrapVec(rgb), a
	end

--#endregion

--#region VisTraceRT

	--- Render target image formats
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @name vistrace_library.VisTraceRTFormat
	-- @class table
	-- @field R8
	-- @field RG88
	-- @field RGB888
	-- @field RF
	-- @field RGFF
	-- @field RGBFFF
	-- @field Albedo
	-- @field Normal
	instance.env.VisTraceRTFormat = VisTraceRTFormat

	--- Creates a render target designed for high performance image processing
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number width
	-- @param number height
	-- @param number format Image format to use for the underlying pixel data (see VisTraceRTFormat)
	-- @param boolean? createMIPs Create mip levels from the current resolution down to 1x1 (Defaults to false. Call generateMIPs to populate these automatically)
	-- @return VisTraceRT
	function vistrace_library.createRenderTarget(width, height, format, createMIPs)
		checkPermission(instance, nil, "vistrace.rendertarget")
		canRun()

		checkLuaType(width, TYPE_NUMBER)
		checkLuaType(height, TYPE_NUMBER)
		checkLuaType(format, TYPE_NUMBER)
		if createMIPs ~= nil then checkLuaType(createMIPs, TYPE_BOOL) end

		return wrapRT(vistrace.CreateRenderTarget(width, height, format, createMIPs))
	end

	--- Returns true if the render target is valid, false otherwise
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return boolean
	function vistracert_methods:isValid()
		canRun()
		return uwrapRT(self):IsValid()
	end

	--- Resizes the render target and optionally creates empty MIP levels
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number width New width
	-- @param number height New height
	-- @param boolean? createMIPs Create mip levels from the current resolution down to 1x1 (Defaults to false. Call generateMIPs to populate these automatically)
	function vistracert_methods:resize(width, height, createMIPs)
		canRun()

		checkLuaType(width, TYPE_NUMBER)
		checkLuaType(height, TYPE_NUMBER)
		if createMIPs ~= nil then checkLuaType(createMIPs, TYPE_BOOL) end

		uwrapRT(self):Resize(width, height, createMIPs)
	end

	--- Gets the width of the specified MIP level
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number? mip Mip level to get the width of (Defaults to 0)
	-- @return number
	function vistracert_methods:getWidth(mip)
		canRun()

		if mip ~= nil then checkLuaType(mip, TYPE_NUMBER) end
		return uwrapRT(self):GetWidth(mip)
	end

	--- Gets the height of the specified MIP level
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number? mip Mip level to get the height of (Defaults to 0)
	-- @return number
	function vistracert_methods:getHeight(mip)
		canRun()

		if mip ~= nil then checkLuaType(mip, TYPE_NUMBER) end
		return uwrapRT(self):GetHeight(mip)
	end

	--- Gets the number of MIP levels in this render target
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number
	function vistracert_methods:getMIPs()
		canRun()
		return uwrapRT(self):GetMIPs()
	end

	--- Gets the format of the render target
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number VisTraceRTFormat
	function vistracert_methods:getFormat()
		canRun()
		return uwrapRT(self):GetFormat()
	end

	--- Gets the colour values of each channel of the pixel, normalised to 0-1
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number x X coordinate of the pixel
	-- @param number y Y coordinate of the pixel
	-- @param number? mip MIP level to get the pixel from (Defaults to 0). This does not transform your X and Y coordinates automatically
	-- @return Vector RGB components of the pixel
	-- @return number Alpha component of the pixel
	function vistracert_methods:getPixel(x, y, mip)
		canRun()

		checkLuaType(x, TYPE_NUMBER)
		checkLuaType(y, TYPE_NUMBER)
		if mip ~= nil then checkLuaType(mip, TYPE_NUMBER) end

		local rgb, a = uwrapRT(self):GetPixel(x, y, mip)
		return wrapVec(rgb), a
	end

	--- Sets the colour values of each channel of the pixel, normalised to 0-1
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number x X coordinate of the pixel
	-- @param number y Y coordinate of the pixel
	-- @param Vector rgb RGB components to write
	-- @param number? a Alpha component to write (Defaults to 1)
	-- @param number? mip MIP level to set the pixel in (Defaults to 0. This does not transform your X and Y coordinates automatically)
	function vistracert_methods:setPixel(x, y, rgb, a, mip)
		canRun()

		checkLuaType(x, TYPE_NUMBER)
		checkLuaType(y, TYPE_NUMBER)
		checkVector(rgb)
		if a ~= nil then checkLuaType(a, TYPE_NUMBER) end
		if mip ~= nil then checkLuaType(mip, TYPE_NUMBER) end

		return uwrapRT(self):SetPixel(x, y, uwrapVec(rgb), a, mip)
	end

	--- Automatically populates the mip levels below the highest with a scaled down version of the texture  
	--- You need to have set createMIPs to true when creating/resizing this render target
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	function vistracert_methods:generateMIPs()
		canRun()
		return uwrapRT(self):GenerateMIPs()
	end

	--- Saves the contents of the render target to a file in garrysmod/data/vistrace
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param string path Path relative to garrysmod/data/vistrace. If a valid extension isn't used, will default to either .png or .hdr depending on RT format
	-- @param number? mip Mip level to save (Defaults to 0)
	function vistracert_methods:save(path, mip)
		canRun()

		checkLuaType(path, TYPE_STRING)
		if mip ~= nil then checkLuaType(mip, TYPE_NUMBER) end

		return uwrapRT(self):Save(path, mip)
	end

	--- Loads the contents of a file in garrysmod/data/vistrace into the render target, resizing the RT to fit
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param string path Path relative to garrysmod/data/vistrace
	-- @param boolean? createMIPs Create mip levels from the file's resolution down to 1x1 (Defaults to false. Call generateMIPs to populate these automatically)
	function vistracert_methods:load(path, createMIPs)
		canRun()

		checkLuaType(path, TYPE_STRING)
		if createMIPs ~= nil then checkLuaType(createMIPs, TYPE_BOOL) end

		return uwrapRT(self):Load(path, createMIPs)
	end

	--- Tonemaps a HDR image target using ACES fitted  
	--- Apply exposure before calling this and gamma correction after
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	function vistracert_methods:tonemap()
		canRun()
		return uwrapRT(self):Tonemap()
	end

--#endregion

--#region VisTraceResult

	--- Gets the hit pos of the result
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return Vector Position of the point of intersection
	function traceresult_methods:pos()
		canRun()
		return wrapVec(uwrapResult(self):Pos())
	end

	--- Gets the incident vector of the result  
	--- This is the inverse of the ray's direction. i.e. it points out from the surface
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return Vector Incident direction
	function traceresult_methods:incident()
		canRun()
		return wrapVec(uwrapResult(self):Incident())
	end

	--- Gets the distance from the ray origin to the hit pos of the result  
	--- This is extremely fast compared to computing the distance between origin and pos yourself, as it's calculated during triangle intersection
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number Distance to the point of intersection
	function traceresult_methods:distance()
		canRun()
		return uwrapResult(self):Distance()
	end

	--- Gets the entity the ray hit
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return Entity Entity that was hit
	function traceresult_methods:entity()
		canRun()
		return wrapEnt(uwrapResult(self):Entity())
	end

	--- Gets the geometric normal of the tri that was hit
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return Vector Geometric normal
	function traceresult_methods:geometricNormal()
		canRun()
		return wrapVec(uwrapResult(self):GeometricNormal())
	end
	--- Gets the shading normal of the intersection
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return Vector Shading normal after weighting and normal mapping
	function traceresult_methods:normal()
		canRun()
		return wrapVec(uwrapResult(self):Normal())
	end
	--- Gets the shading tangent of the intersection
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return Vector Shading tangent after weighting and normal mapping
	function traceresult_methods:tangent()
		canRun()
		return wrapVec(uwrapResult(self):Tangent())
	end
	--- Gets the shading binormal of the intersection
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return Vector Shading binormal after weighting and normal mapping
	function traceresult_methods:binormal()
		canRun()
		return wrapVec(uwrapResult(self):Binormal())
	end

	--- Gets the barycentric coordinates of the intersection
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return Vector Vector containing the UVW of the intersection mapped to XYZ
	function traceresult_methods:barycentric()
		canRun()
		return wrapVec(uwrapResult(self):Barycentric())
	end

	--- Gets the texture UV of the intersection
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return table Table with keys u, v
	function traceresult_methods:textureUV()
		canRun()
		return uwrapResult(self):TextureUV()
	end

	--- Gets the submaterial index of the hit tri
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number Submat index
	function traceresult_methods:subMaterialIndex()
		canRun()
		return uwrapResult(self):SubMaterialIndex()
	end

	--- Gets the albedo of the intersection after applying entity colour and base texture
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return Vector Colour normalised to 0-1
	function traceresult_methods:albedo()
		canRun()
		return wrapVec(uwrapResult(self):Albedo())
	end
	--- Gets the alpha of the intersection after applying entity colour and base texture
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number Alpha normalised to 0-1
	function traceresult_methods:alpha()
		canRun()
		return uwrapResult(self):Alpha()
	end
	--- Gets the metalness of the intersection after applying MRAO texture
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number Metalness normailsed to 0-1 (defaults to 0 if no MRAO found)
	function traceresult_methods:metalness()
		canRun()
		return uwrapResult(self):Metalness()
	end
	--- Gets the roughness of the intersection after applying MRAO texture
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number roughness normailsed to 0-1 (defaults to 1 if no MRAO found)
	function traceresult_methods:roughness()
		canRun()
		return uwrapResult(self):Roughness()
	end

	--- Gets the material's $flags value
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number Bitflags
	function traceresult_methods:materialFlags()
		canRun()
		return uwrapResult(self):MaterialFlags()
	end
	--- Gets the material's SURF flags (only present on world)
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number Bitflags
	function traceresult_methods:surfaceFlags()
		canRun()
		return uwrapResult(self):SurfaceFlags()
	end

	--- Gets whether we hit the sky
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return boolean True if we hit the sky of the map
	function traceresult_methods:hitSky()
		canRun()
		return uwrapResult(self):HitSky()
	end

	--- Gets whether we hit water
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return boolean True if we hit a tri using a water shader
	function traceresult_methods:hitWater()
		canRun()
		return uwrapResult(self):HitWater()
	end

	--- Gets whether we hit the front side of a tri
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return boolean True if we hit the front face of a tri
	function traceresult_methods:frontFacing()
		canRun()
		return uwrapResult(self):FrontFacing()
	end

	--- Gets the floating point MIP level that was sampled from the base texture  
	--- Mainly used for debugging texture LoD calculation
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number Floating point MIP level (where 0 is the highest resolution)
	function traceresult_methods:baseMIPLevel()
		canRun()
		return uwrapResult(self):BaseMIPLevel()
	end

	--- Gets the path to the hit material, relative to the materials folder
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return string Material path
	function traceresult_methods:material()
		canRun()
		return uwrapResult(self):Material()
	end

	--- Gets the path to the hit material's base texture, relative to the materials folder
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return string? Base texture path
	function traceresult_methods:baseTexture()
		canRun()
		return uwrapResult(self):BaseTexture()
	end
	--- Gets the path to the hit material's normal map, relative to the materials folder
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return string? Normal map path
	function traceresult_methods:normalMap()
		canRun()
		return uwrapResult(self):NormalMap()
	end
	--- Gets the path to the hit material's metalness, roughness, and ambient occlusion texture, relative to the materials folder
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return string? MRAO path
	function traceresult_methods:mrao()
		canRun()
		return uwrapResult(self):MRAO()
	end

	--- Gets the path to the hit material's second base texture, relative to the materials folder  
	--- This will usually only be present on displacement surfaces
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return string? Second base texture path
	function traceresult_methods:baseTexture2()
		canRun()
		return uwrapResult(self):BaseTexture2()
	end
	--- Gets the path to the hit material's second normal map, relative to the materials folder  
	--- This will usually only be present on displacement surfaces
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return string? Second normal map path
	function traceresult_methods:normalMap2()
		canRun()
		return uwrapResult(self):NormalMap2()
	end
	--- Gets the path to the hit material's second MRAO texture, relative to the materials folder  
	--- This will usually only be present on displacement surfaces
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return string? Second MRAO path
	function traceresult_methods:mrao2()
		canRun()
		return uwrapResult(self):MRAO2()
	end

	--- Gets the path to the hit material's blend texture, relative to the materials folder  
	--- This will usually only be present on displacement surfaces
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return string? Blend texture path
	function traceresult_methods:blendTexture()
		canRun()
		return uwrapResult(self):BlendTexture()
	end
	--- Gets the path to the hit material's detail texture, relative to the materials folder
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return string? Detail texture path
	function traceresult_methods:detailTexture()
		canRun()
		return uwrapResult(self):DetailTexture()
	end

--#endregion

--#region AccelStruct

	--- Rebuild the acceleration structure
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
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
				if debug_getmetatable(v) ~= ent_meta then SF.ThrowTypeError("Entity", SF.GetType(v), 2, "Entity table entry not an entity.") end
				unwrapped[k] = uwrapEnt(v)
			end
			entities = unwrapped
		end
		uwrapAccel(self):Rebuild(entities, traceWorld)
	end

	--- Traverses the acceleration structure
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param Vector origin Ray origin
	-- @param Vector direction Ray direction
	-- @param number? tMin Minimum distance of the ray (basically offset from start along direction)
	-- @param number? tMax Maximum distance of the ray
	-- @param number? coneWidth Starting width of the ray cone (Defaults to -1 which disables mipmapping)
	-- @param number? coneAngle Starting angle of the ray cone (Defaults to -1 which disables mipmapping)
	-- @return VisTraceResult? Result of the traversal, or nil if ray missed
	function accelstruct_methods:traverse(origin, direction, tMin, tMax, coneWidth, coneAngle)
		canRun()

		checkVector(origin)
		validateVector(origin)

		checkVector(direction)
		validateVector(direction)

		if tMin then checkLuaType(tMin, TYPE_NUMBER) end
		if tMax then checkLuaType(tMax, TYPE_NUMBER) end
		if coneWidth then checkLuaType(coneWidth, TYPE_NUMBER) end
		if coneAngle then checkLuaType(coneAngle, TYPE_NUMBER) end

		local res = uwrapAccel(self):Traverse(uwrapVec(origin), uwrapVec(direction), tMin, tMax, coneWidth, coneAngle)
		if res then
			return wrapResult(res)
		end
	end

	--- Creates an acceleration struction (AccelStruct)
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
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
				if debug_getmetatable(v) ~= ent_meta then SF.ThrowTypeError("Entity", SF.GetType(v), 2, "Entity table entry not an entity.") end
				unwrapped[k] = uwrapEnt(v)
			end
			entities = unwrapped
		end
		return wrapAccel(vistrace.CreateAccel(entities, traceWorld))
	end

--#endregion

--#region Sampler

	--- Gets a uniform random float from the sampler
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number Random float in a 0-1 range
	function sampler_methods:getFloat()
		canRun()
		return uwrapSampler(self):GetFloat()
	end

	--- Gets two uniform random floats from the sampler
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return number Random float in a 0-1 range
	-- @return number Random float in a 0-1 range
	function sampler_methods:getFloat2D()
		canRun()
		return uwrapSampler(self):GetFloat2D()
	end

	--- Creates a random sampler
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
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
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return BSDFMaterial New material object
	function vistrace_library.createMaterial()
		canRun()
		return wrapMat(vistrace.CreateMaterial())
	end

	--- Set the colour of the material (multiplies by the sampled colour)
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param Vector? colour Colour to set as a 0-1 normalised vector
	-- @return Vector? Dielectric colour
	-- @return Vector? Conductor colour
	function bsdfmaterial_methods:colour(colour)
		canRun()

		if colour ~= nil then
			uwrapMat(self):Colour(uwrapVec(colour))
		else
			local d, c = uwrapMat(self):Colour()
			return wrapVec(d), wrapVec(c)
		end
	end

	--- Set the colour of the material's dielectric lobes  
	--- Overridden by BSDFMaterial:colour()
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param Vector? colour Colour to set as a 0-1 normalised vector
	-- @return Vector? Dielectric colour
	function bsdfmaterial_methods:dielectricColour(colour)
		canRun()

		if colour ~= nil then
			uwrapMat(self):DielectricColour(uwrapVec(colour))
		else
			return wrapVec(uwrapMat(self):DielectricColour())
		end
	end

	--- Set the colour of the material's conductor lobes  
	--- Overridden by BSDFMaterial:colour()
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param Vector? colour Colour to set as a 0-1 normalised vector
	-- @return Vector? Conductor colour
	function bsdfmaterial_methods:conductorColour(colour)
		canRun()

		if colour ~= nil then
			uwrapMat(self):ConductorColour(uwrapVec(colour))
		else
			return wrapVec(uwrapMat(self):ConductorColour())
		end
	end

	--- Set the colour of conductors at grazing angles (useful for anodized materials)
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param Vector? colour Colour to set as a 0-1 normalised vector
	-- @return Vector? Edge tint colour
	function bsdfmaterial_methods:edgeTint(colour)
		canRun()

		if colour ~= nil then
			uwrapMat(self):EdgeTint(uwrapVec(colour))
		else
			return wrapVec(uwrapMat(self):EdgeTint())
		end
	end

	--- Set the falloff of the conductor's edge tint
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number? falloff Falloff value (0.2 is equivalent to Fresnel)
	-- @return number? Falloff value
	function bsdfmaterial_methods:edgeTintFalloff(falloff)
		canRun()

		if falloff ~= nil then
			checkLuaType(falloff, TYPE_NUMBER)
			uwrapMat(self):EdgeTintFalloff(falloff)
		else
			return uwrapMat(self):EdgeTintFalloff()
		end
	end

	--- Set the metalness of the material (overrides PBR textures)
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number? metalness Metalness to set
	-- @return number? Metalness
	function bsdfmaterial_methods:metalness(metalness)
		canRun()

		if metalness ~= nil then
			checkLuaType(metalness, TYPE_NUMBER)
			uwrapMat(self):Metalness(metalness)
		else
			return uwrapMat(self):Metalness()
		end
	end
	--- Set the roughness of the material (overrides PBR textures)
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number? roughness Roughness to set
	-- @return number? Roughness
	function bsdfmaterial_methods:roughness(roughness)
		canRun()

		if roughness ~= nil then
			checkLuaType(roughness, TYPE_NUMBER)
			uwrapMat(self):Roughness(roughness)
		else
			return uwrapMat(self):Roughness()
		end
	end

	--- Set how anisotropic the specular reflection is in the tangent direction
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number? anisotropy 0 - isotropic, 1 - anisotropic in the tangent direction
	-- @return number? Anisotropy
	function bsdfmaterial_methods:anisotropy(anisotropy)
		canRun()

		if anisotropy ~= nil then
			checkLuaType(anisotropy, TYPE_NUMBER)
			uwrapMat(self):Anisotropy(anisotropy)
		else
			return uwrapMat(self):Anisotropy()
		end
	end
	--- Set the amount to rotate the tangent frame by before localising (for anisotropic materials)
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number? rotation Rotation of the tangent and binormal about the normal where 0 is no rotation and 1 is a full circle
	-- @return number? Anisotropic rotation
	function bsdfmaterial_methods:anisotropicRotation(rotation)
		canRun()

		if rotation ~= nil then
			checkLuaType(rotation, TYPE_NUMBER)
			uwrapMat(self):AnisotropicRotation(rotation)
		else
			return uwrapMat(self):AnisotropicRotation()
		end
	end

	--- Set the index of refraction of the material
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number? ior Index of refraction to set
	-- @return number? Index of refraction
	function bsdfmaterial_methods:ior(ior)
		canRun()

		if ior ~= nil then
			checkLuaType(ior, TYPE_NUMBER)
			uwrapMat(self):IoR(ior)
		else
			return uwrapMat(self):IoR()
		end
	end

	--- Set the diffuse transmission amount
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number? diffuseTransmission Not currently implemented
	-- @return number? Diffuse transmission
	function bsdfmaterial_methods:diffuseTransmission(diffuseTransmission)
		canRun()

		if diffuseTransmission ~= nil then
			checkLuaType(diffuseTransmission, TYPE_NUMBER)
			uwrapMat(self):DiffuseTransmission(diffuseTransmission)
		else
			return uwrapMat(self):DiffuseTransmission()
		end
	end
	--- Set the specular transmission amount
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number? specularTransmission Blend between diffuse/specular lobes and specular reflection/transmission lobes
	-- @return number? Specular transmission
	function bsdfmaterial_methods:specularTransmission(specularTransmission)
		canRun()

		if specularTransmission ~= nil then
			checkLuaType(specularTransmission, TYPE_NUMBER)
			uwrapMat(self):SpecularTransmission(specularTransmission)
		else
			return uwrapMat(self):SpecularTransmission()
		end
	end

	--- Toggle thin film
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param boolean? thin True to simulate the material as thin film
	-- @return boolean? Whether the material is thin
	function bsdfmaterial_methods:thin(thin)
		canRun()

		if thin ~= nil then
			checkLuaType(thin, TYPE_BOOL)
			uwrapMat(self):Thin(thin)
		else
			return uwrapMat(self):Thin()
		end
	end

	--- Sets which BSDF lobes should be sampled/evaluated
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param number? LobeType flags to set
	-- @return number? Active LobeType flags
	function bsdfmaterial_methods:activeLobes(lobes)
		canRun()

		if lobes ~= nil then
			checkLuaType(lobes, TYPE_NUMBER)
			uwrapMat(self):ActiveLobes(lobes)
		else
			return uwrapMat(self):ActiveLobes()
		end
	end

	--- Gets the entity's BSDFMaterial
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return BSDFMaterial Material object
	function ents_methods:getBSDFMaterial()
		canRun()
		return wrapMat(uwrapEnt(self):GetBSDFMaterial())
	end

--#endregion

--#region BSDF

	--- BSDF Lobes
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @name vistrace_library.LobeType
	-- @class table
	-- @field None
	-- @field DiffuseReflection
	-- @field DiffuseTransmission
	-- @field SpecularReflection
	-- @field SpecularTransmission
	-- @field ConductiveReflection
	-- @field DeltaSpecularReflection
	-- @field DeltaSpecularTransmission
	-- @field DeltaConductiveReflection
	-- @field Reflection
	-- @field Transmission
	-- @field Delta
	-- @field NonDelta
	-- @field Diffuse
	-- @field Specular
	-- @field Dielectric
	-- @field Conductive
	-- @field All
	instance.env.LobeType = LobeType

	--- Importance samples the VisTrace BSDF
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param Sampler sampler Sampler object
	-- @param BSDFMaterial material Material parameters
	-- @return table? sample Sample generated (if valid)
	function traceresult_methods:sampleBSDF(sampler, material)
		canRun()

		if debug_getmetatable(sampler) ~= sampler_meta then SF.ThrowTypeError("Sampler", SF.GetType(sampler), 2) end
		if debug_getmetatable(material) ~= bsdfmaterial_meta then SF.ThrowTypeError("BSDFMaterial", SF.GetType(material), 2) end

		local sample = uwrapResult(self):SampleBSDF(uwrapSampler(sampler), uwrapMat(material))
		if sample then
			return {
				scattered = wrapVec(sample.scattered),
				pdf = sample.pdf,
				weight = wrapVec(sample.weight),
				lobe = sample.lobe
			}
		end
	end

	--- Evaluates the VisTrace BSDF
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param BSDFMaterial material Material parameters
	-- @param Vector scattered Scattered light direction
	-- @return Vector Evaluated surface colour
	function traceresult_methods:evalBSDF(material, scattered)
		canRun()

		if debug_getmetatable(material) ~= bsdfmaterial_meta then SF.ThrowTypeError("BSDFMaterial", SF.GetType(material), 2) end

		checkVector(scattered)
		validateVector(scattered)

		return wrapVec(uwrapResult(self):EvalBSDF(uwrapMat(material), uwrapVec(scattered)))
	end

	--- Evaluates the VisTrace BSDF's PDF
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param BSDFMaterial material Material parameters
	-- @param Vector scattered Scattered light direction
	-- @return number Evaluated PDF
	function traceresult_methods:evalPDF(material, scattered)
		canRun()

		if debug_getmetatable(material) ~= bsdfmaterial_meta then SF.ThrowTypeError("BSDFMaterial", SF.GetType(material), 2) end

		checkVector(scattered)
		validateVector(scattered)

		return uwrapResult(self):EvalPDF(uwrapMat(material), uwrapVec(scattered))
	end

	--- Importance samples the VisTrace BSDF
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param Sampler sampler Sampler object
	-- @param BSDFMaterial material Material parameters
	-- @param Vector normal Normal of the surface
	-- @param Vector tangent Tangent of the surface
	-- @param Vector binormal Binormal of the surface
	-- @param Vector incident Incident vector (negative direction of the ray that hit the surface)
	-- @return table? sample Sample generated (if valid)
	function vistrace_library.sampleBSDF(sampler, material, normal, tangent, binormal, incident)
		canRun()

		if debug_getmetatable(sampler) ~= sampler_meta then SF.ThrowTypeError("Sampler", SF.GetType(sampler), 2) end
		if debug_getmetatable(material) ~= bsdfmaterial_meta then SF.ThrowTypeError("BSDFMaterial", SF.GetType(material), 2) end

		checkVector(normal)
		validateVector(normal)
		checkVector(tangent)
		validateVector(tangent)
		checkVector(binormal)
		validateVector(binormal)

		checkVector(incident)
		validateVector(incident)

		local sample = vistrace.SampleBSDF(
			uwrapSampler(sampler), uwrapMat(material),
			uwrapVec(normal), uwrapVec(tangent), uwrapVec(binormal),
			uwrapVec(incident)
		)
		if sample then
			return {
				scattered = wrapVec(sample.scattered),
				pdf = sample.pdf,
				weight = wrapVec(sample.weight),
				lobe = sample.lobe
			}
		end
	end

	--- Evaluates the VisTrace BSDF
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param BSDFMaterial material Material parameters
	-- @param Vector normal Normal of the surface
	-- @param Vector tangent Tangent of the surface
	-- @param Vector binormal Binormal of the surface
	-- @param Vector incident Incident vector (negative direction of the ray that hit the surface)
	-- @param Vector scattered Scattered light direction
	-- @return Vector Evaluated surface colour
	function vistrace_library.evalBSDF(material, normal, tangent, binormal, incident, scattered)
		canRun()

		if debug_getmetatable(material) ~= bsdfmaterial_meta then SF.ThrowTypeError("BSDFMaterial", SF.GetType(material), 2) end

		checkVector(normal)
		validateVector(normal)
		checkVector(tangent)
		validateVector(tangent)
		checkVector(binormal)
		validateVector(binormal)

		checkVector(incident)
		validateVector(incident)

		checkVector(scattered)
		validateVector(scattered)

		return wrapVec(vistrace.EvalBSDF(
			uwrapMat(material),
			uwrapVec(normal), uwrapVec(tangent), uwrapVec(binormal),
			uwrapVec(incident), uwrapVec(scattered)
		))
	end

	--- Evaluates the VisTrace BSDF's PDF
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param BSDFMaterial material Material parameters
	-- @param Vector normal Normal of the surface
	-- @param Vector tangent Tangent of the surface
	-- @param Vector binormal Binormal of the surface
	-- @param Vector incident Incident vector (negative direction of the ray that hit the surface)
	-- @param Vector scattered Scattered light direction
	-- @return number Evaluated PDF
	function vistrace_library.evalPDF(material, normal, tangent, binormal, incident, scattered)
		canRun()

		if debug_getmetatable(material) ~= bsdfmaterial_meta then SF.ThrowTypeError("BSDFMaterial", SF.GetType(material), 2) end

		checkVector(normal)
		validateVector(normal)
		checkVector(tangent)
		validateVector(tangent)
		checkVector(binormal)
		validateVector(binormal)

		checkVector(incident)
		validateVector(incident)

		checkVector(scattered)
		validateVector(scattered)

		return vistrace.EvalPDF(
			uwrapMat(material),
			uwrapVec(normal), uwrapVec(tangent), uwrapVec(binormal),
			uwrapVec(incident), uwrapVec(scattered)
		)
	end

--#endregion

--#region HDRI

	--- Loads a HDRI from garrysmod/data/vistrace_hdris and appends the .hdr extension automatically  
	--- Subfolders are allowed
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
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
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @return boolean Whether the HDRI is valid
	function hdri_methods:isValid()
		canRun()
		return uwrapHDRI(self):IsValid()
	end

	--- Samples a pixel from the HDRI
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param Vector direction Direction to get the colour of
	-- @return Vector Colour value
	function hdri_methods:getPixel(direction)
		canRun()
		checkVector(direction)
		validateVector(direction)
		return wrapVec(uwrapHDRI(self):GetPixel(uwrapVec(direction)))
	end

	--- Calculates the probability of sampling this direction
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param Vector direction Direction to get the probability of
	-- @return number Probability
	function hdri_methods:evalPDF(direction)
		canRun()
		checkVector(direction)
		validateVector(direction)

		return uwrapHDRI(self):EvalPDF(uwrapVec(direction))
	end

	--- Importance samples the HDRI (only the first value is returned if the sample is invalid)
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
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
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
	-- @param Angle angle Angle to set
	function hdri_methods:setAngles(angle)
		canRun()
		if debug_getmetatable(angle) ~= angleMetaTbl then SF.ThrowTypeError("Angle", SF.GetType(angle), 2) end
		uwrapHDRI(self):SetAngles(uwrapAng(angle))
	end

--#endregion

	--- Calculates a biased offset from an intersection point to prevent self intersection
	-- @src https://github.com/Derpius/VisTrace/blob/addon/lua/starfall/libs_cl/vistrace_sf.lua
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

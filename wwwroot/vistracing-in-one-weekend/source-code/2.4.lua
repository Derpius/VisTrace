----------------
-- Parameters --
----------------
local RESX, RESY = 256, 256
local SAMPLES = 8
local MAX_DEPTH = 8

local FOCAL_LENGTH_MM = 60
local SENSOR_HEIGHT_MM = 24

----------------
--    Init    --
----------------
local accel = vistrace.CreateAccel(ents.FindByClass("prop_*"), false)
local hdri = vistrace.LoadHDRI("drackenstein_quarry_4k")
local sampler = vistrace.CreateSampler()

local sensorWidth = SENSOR_HEIGHT_MM * RESX / RESY

local halfSensorWidth = sensorWidth / 2
local halfSensorHeight = SENSOR_HEIGHT_MM / 2

local sensorWidthDivRes = sensorWidth / RESX
local sensorHeightDivRes = SENSOR_HEIGHT_MM / RESY

local rt = GetRenderTargetEx(
	"VisTracer",                     -- Name of the render target
	1, 1, RT_SIZE_FULL_FRAME_BUFFER, -- Resize to screen res automatically
	MATERIAL_RT_DEPTH_SEPARATE,      -- Create a dedicated depth/stencil buffer
	bit.bor(1, 256),                 -- Texture flags for point sampling and no mips
	0,                               -- No RT flags
	IMAGE_FORMAT_RGBA8888            -- RGB image format with 8 bits per channel
)

local rtMat = CreateMaterial("VisTracer", "UnlitGeneric", {
	["$basetexture"] = rt:GetName(),
	["$translucent"] = "1" -- Enables transparency on the material
})

local function Reflect(i, n)
	return i - 2 * i:Dot(n) * n
end

local function FresnelSchlicks(i, n, f0)
	return f0 + (Vector(1, 1, 1) - f0) * math.pow(1 - math.abs(i:Dot(n)), 5)
end

local camPos, camAng = LocalPlayer():EyePos(), LocalPlayer():EyeAngles()
local function TracePixel(x, y)
	local camX = halfSensorWidth - sensorWidthDivRes * (x + 0.5)
	local camY = halfSensorHeight - sensorHeightDivRes * (y + 0.5)

	local camDir = Vector(FOCAL_LENGTH_MM, camX, camY)
	camDir:Rotate(camAng)
	camDir:Normalize()

	local result = accel:Traverse(camPos, camDir)
	if result then
		local direction = camDir
		local colour = Vector()
		local throughput = Vector(1, 1, 1)

		for depth = 1, MAX_DEPTH do
			local origin = vistrace.CalcRayOrigin(result:Pos(), result:GeometricNormal())
			local diffuse = true
			if result:Entity():IsValid() then
				diffuse = result:Entity():GetMaterial() ~= "debug/env_cubemap_model"
			end

			if diffuse then
				local validSamples = SAMPLES
				for i = 1, SAMPLES do
					local envValid, envDir, envCol, envPdf = hdri:Sample(sampler)
					if envValid then
						local shadowRay = accel:Traverse(origin, envDir)
						if not shadowRay then
							local brdf = result:Albedo() / math.pi
							local Li = envCol / envPdf
							colour = colour + brdf * Li * math.max(envDir:Dot(result:Normal()), 0)
						end
					else
						validSamples = validSamples - 1
					end
				end

				colour = colour / validSamples * throughput
				break
			else
				direction = Reflect(direction, result:Normal())
				local f0 = result:Entity():GetColor()
				throughput = throughput * FresnelSchlicks(
					direction, result:Normal(),
					Vector(f0.r / 255, f0.g / 255, f0.b / 255)
				)
				result = accel:Traverse(origin, direction)

				if not result then
					colour = hdri:GetPixel(direction) * throughput
					break
				end
			end
		end

		return colour
	else
		return hdri:GetPixel(camDir)
	end
end

local y = 0
local setup = true
hook.Add("HUDPaint", "VisTracer", function()
	if y < RESY then
		render.PushRenderTarget(rt)
		if setup then
			render.Clear(0, 0, 0, 0, true, true)
			setup = false
		end

		for x = 0, RESX - 1 do
			local rgb = TracePixel(x, y)

			render.SetViewPort(x, y, 1, 1)
			render.Clear(
				math.Clamp(rgb[1] * 255, 0, 255),
				math.Clamp(rgb[2] * 255, 0, 255),
				math.Clamp(rgb[3] * 255, 0, 255),
				255, true, true
			)
		end

		render.PopRenderTarget()
		y = y + 1
	end

	render.SetMaterial(rtMat)
	render.DrawScreenQuad() -- Draws a quad to the entire screen
end)

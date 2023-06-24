local RESX, RESY = 256, 256

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
			render.SetViewPort(x, y, 1, 1)
			render.Clear(x / (RESX - 1) * 255, y / (RESY - 1) * 255, 0, 255, true, true)
		end

		render.PopRenderTarget()
		y = y + 1
	end

	render.SetMaterial(rtMat)
	render.DrawScreenQuad() -- Draws a quad to the entire screen
end)

TOOL.Category = "Render"
TOOL.Name = "#tool.bsdf_material.name"

TOOL.Information = {
	{name = "left"},
	{name = "right"},
	{name = "reload"}
}

TOOL.ClientConVar.separatecolours = 0

TOOL.ClientConVar.dielectric_r = 255
TOOL.ClientConVar.dielectric_g = 255
TOOL.ClientConVar.dielectric_b = 255

TOOL.ClientConVar.conductive_r = 255
TOOL.ClientConVar.conductive_g = 255
TOOL.ClientConVar.conductive_b = 255

TOOL.ClientConVar.edgetint_r = 255
TOOL.ClientConVar.edgetint_g = 255
TOOL.ClientConVar.edgetint_b = 255
TOOL.ClientConVar.falloff    = 0.2

TOOL.ClientConVar.roughnessoverride = 0
TOOL.ClientConVar.roughness = 1
TOOL.ClientConVar.metalnessoverride = 0
TOOL.ClientConVar.metalness = 0

TOOL.ClientConVar.anisotropy = 0
TOOL.ClientConVar.anisotropicrotation = 0

TOOL.ClientConVar.ior = 1.5
TOOL.ClientConVar.difftrans = 0
TOOL.ClientConVar.spectrans = 0
TOOL.ClientConVar.thin = 0

-- Dupe support
local function SetMaterial(plr, ent, materialTbl)
	if CLIENT then return true end

	ent.BSDFMaterial = materialTbl
	if materialTbl then
		duplicator.StoreEntityModifier(ent, "BSDFMaterial", materialTbl)
		ent:SetNWBool("BSDFMaterial.Set", true)

		ent:SetNWVector("BSDFMaterial.Dielectric", materialTbl.Dielectric)
		ent:SetNWVector("BSDFMaterial.Conductive", materialTbl.Conductive)

		ent:SetNWVector("BSDFMaterial.EdgeTint", materialTbl.EdgeTint)
		ent:SetNWFloat("BSDFMaterial.Falloff", materialTbl.Falloff)

		if materialTbl.Roughness then
			ent:SetNWBool("BSDFMaterial.RoughnessOverride", true)
			ent:SetNWFloat("BSDFMaterial.Roughness", materialTbl.Roughness)
		else
			ent:SetNWBool("BSDFMaterial.RoughnessOverride", false)
		end

		if materialTbl.Metalness then
			ent:SetNWBool("BSDFMaterial.MetalnessOverride", true)
			ent:SetNWFloat("BSDFMaterial.Metalness", materialTbl.Metalness)
		else
			ent:SetNWBool("BSDFMaterial.MetalnessOverride", false)
		end

		ent:SetNWFloat("BSDFMaterial.Anisotropy", materialTbl.Anisotropy)
		ent:SetNWFloat("BSDFMaterial.AnisotropicRotation", materialTbl.AnisotropicRotation)

		ent:SetNWFloat("BSDFMaterial.IoR", materialTbl.IoR)
		ent:SetNWFloat("BSDFMaterial.DiffuseTransmission", materialTbl.DiffuseTransmission)
		ent:SetNWFloat("BSDFMaterial.SpecularTransmission", materialTbl.SpecularTransmission)
		ent:SetNWBool("BSDFMaterial.Thin", materialTbl.Thin)
	else
		duplicator.ClearEntityModifier(ent, "BSDFMaterial")
		ent:SetNWBool("BSDFMaterial.Set", false)
	end

	return true
end

duplicator.RegisterEntityModifier("BSDFMaterial", SetMaterial)

-- Apply
function TOOL:LeftClick(trace)
	local ent = trace.Entity
	if IsValid(ent.AttachedEntity) then ent = ent.AttachedEntity end

	if not IsValid(ent) then return false end
	if CLIENT then return true end

	local dielectric_r = self:GetClientNumber("dielectric_r", 255)
	local dielectric_g = self:GetClientNumber("dielectric_g", 255)
	local dielectric_b = self:GetClientNumber("dielectric_b", 255)
	local conductive_r = self:GetClientNumber("conductive_r", 255)
	local conductive_g = self:GetClientNumber("conductive_g", 255)
	local conductive_b = self:GetClientNumber("conductive_b", 255)

	local edgetint_r = self:GetClientNumber("edgetint_r", 255)
	local edgetint_g = self:GetClientNumber("edgetint_g", 255)
	local edgetint_b = self:GetClientNumber("edgetint_b", 255)
	local falloff = self:GetClientNumber("falloff", 0.2)

	local roughness
	if self:GetClientNumber("roughnessoverride", 0) ~= 0 then
		roughness = self:GetClientNumber("roughness", -1)
	end

	local metalness
	if self:GetClientNumber("metalnessoverride", 0) ~= 0 then
		metalness = self:GetClientNumber("metalness", -1)
	end

	local anisotropy = self:GetClientNumber("anisotropy", 0)
	local anisotropicrotation = self:GetClientNumber("anisotropicrotation", 0)

	local ior = self:GetClientNumber("ior", 1.5)
	local difftrans = self:GetClientNumber("difftrans", 0)
	local spectrans = self:GetClientNumber("spectrans", 0)
	local thin = self:GetClientNumber("thin", 0) ~= 0

	SetMaterial(self:GetOwner(), ent, {
		Dielectric = Vector(dielectric_r, dielectric_g, dielectric_b) / 255,
		Conductive = Vector(conductive_r, conductive_g, conductive_b) / 255,

		EdgeTint = Vector(edgetint_r, edgetint_g, edgetint_b) / 255,
		Falloff = falloff,

		Roughness = roughness,
		Metalness = metalness,

		Anisotropy = anisotropy,
		AnisotropicRotation = anisotropicrotation / 360,

		IoR = ior,
		DiffuseTransmission = difftrans,
		SpecularTransmission = spectrans,
		Thin = thin
	})
	return true
end

-- Copy
function TOOL:RightClick(trace)
	local ent = trace.Entity
	if IsValid(ent.AttachedEntity) then ent = ent.AttachedEntity end

	if not IsValid(ent) then return false end
	if CLIENT then return true end

	local mat = ent.BSDFMaterial or {
		Dielectric = Vector(1, 1, 1),
		Conductive = Vector(1, 1, 1),

		EdgeTint = Vector(1, 1, 1),
		Falloff = 0.2,

		Anisotropy = 0,
		AnisotropicRotation = 0,

		IoR = 1.5,
		DiffuseTransmission = 0,
		SpecularTransmission = 0,
		Thin = false
	}

	self:GetOwner():ConCommand("bsdf_material_dielectric_r "        .. mat.Dielectric[1] * 255)
	self:GetOwner():ConCommand("bsdf_material_dielectric_g "        .. mat.Dielectric[2] * 255)
	self:GetOwner():ConCommand("bsdf_material_dielectric_b "        .. mat.Dielectric[3] * 255)
	self:GetOwner():ConCommand("bsdf_material_conductive_r "        .. mat.Conductive[1] * 255)
	self:GetOwner():ConCommand("bsdf_material_conductive_g "        .. mat.Conductive[2] * 255)
	self:GetOwner():ConCommand("bsdf_material_conductive_b "        .. mat.Conductive[3] * 255)

	self:GetOwner():ConCommand("bsdf_material_edgetint_r "          .. mat.EdgeTint[1] * 255)
	self:GetOwner():ConCommand("bsdf_material_edgetint_g "          .. mat.EdgeTint[2] * 255)
	self:GetOwner():ConCommand("bsdf_material_edgetint_b "          .. mat.EdgeTint[3] * 255)
	self:GetOwner():ConCommand("bsdf_material_falloff "             .. mat.Falloff)

	self:GetOwner():ConCommand("bsdf_material_roughnessoverride 0")
	self:GetOwner():ConCommand("bsdf_material_metalnessoverride 0")

	self:GetOwner():ConCommand("bsdf_material_anisotropy "          .. mat.Anisotropy)
	self:GetOwner():ConCommand("bsdf_material_anisotropicrotation " .. mat.AnisotropicRotation * 360)

	self:GetOwner():ConCommand("bsdf_material_ior "                 .. mat.IoR)
	self:GetOwner():ConCommand("bsdf_material_difftrans "           .. mat.DiffuseTransmission)
	self:GetOwner():ConCommand("bsdf_material_spectrans "           .. mat.SpecularTransmission)
	self:GetOwner():ConCommand("bsdf_material_thin "                .. (mat.Thin and "1" or "0"))

	return true
end

-- Clear
function TOOL:Reload(trace)
	local ent = trace.Entity
	if IsValid(ent.AttachedEntity) then ent = ent.AttachedEntity end

	if not IsValid(ent) then return false end
	if CLIENT then return true end

	SetMaterial(self:GetOwner(), ent, nil)
	return true
end

if CLIENT then
	language.Add("tool.bsdf_material.name", "BSDF Material")
	language.Add("tool.bsdf_material.desc", "Change the BSDF Material used by VisTrace of an object")
	language.Add("tool.bsdf_material.left", "Apply the current material")
	language.Add("tool.bsdf_material.right", "Copy the material of an entity")
	language.Add("tool.bsdf_material.reload", "Reset the material of an entity")

	local entity = FindMetaTable("Entity")
	function entity:GetBSDFMaterial()
		if not vistrace then
			error("The required version (v" .. VISTRACE_VERSION .. ") of the VisTrace binary module is not installed (get it here https://github.com/Derpius/VisTrace/releases)")
		end

		local mat = vistrace.CreateMaterial()
		if not self:GetNWBool("BSDFMaterial.Set") then return mat end

		mat:DielectricColour(self:GetNWVector("BSDFMaterial.Dielectric"))
		mat:ConductorColour(self:GetNWVector("BSDFMaterial.Conductive"))

		mat:EdgeTint(self:GetNWVector("BSDFMaterial.EdgeTint"))
		mat:EdgeTintFalloff(self:GetNWFloat("BSDFMaterial.Falloff"))

		if self:GetNWBool("BSDFMaterial.RoughnessOverride") then
			mat:Roughness(self:GetNWFloat("BSDFMaterial.Roughness"))
		end

		if self:GetNWBool("BSDFMaterial.MetalnessOverride") then
			mat:Metalness(self:GetNWFloat("BSDFMaterial.Metalness"))
		end

		mat:Anisotropy(self:GetNWFloat("BSDFMaterial.Anisotropy"))
		mat:AnisotropicRotation(self:GetNWFloat("BSDFMaterial.AnisotropicRotation"))

		mat:IoR(self:GetNWFloat("BSDFMaterial.IoR"))
		mat:DiffuseTransmission(self:GetNWFloat("BSDFMaterial.DiffuseTransmission"))
		mat:SpecularTransmission(self:GetNWFloat("BSDFMaterial.SpecularTransmission"))
		mat:Thin(self:GetNWBool("BSDFMaterial.Thin"))

		return mat
	end

	local function DirToCubemap(map, dir, mip)
		local absX, absY, absZ = math.abs(dir[1]), math.abs(dir[2]), math.abs(dir[3])
		local posX, posY, posZ = dir[1] > 0, dir[2] > 0, dir[3] > 0

		local maxAxis = math.max(absX, absY, absZ)
		local u, v, face

		if maxAxis == absX then
			if posX then
				u = -dir[3]
				v = dir[2]
				face = 0
			else
				u = dir[3]
				v = dir[2]
				face = 1
			end
		elseif maxAxis == absY then
			if posY then
				u = dir[1]
				v = -dir[3]
				face = 2
			else
				u = dir[1]
				v = dir[3]
				face = 3
			end
		else
			if posZ then
				u = dir[1]
				v = dir[2]
				face = 4
			else
				u = -dir[1]
				v = dir[2]
				face = 5
			end
		end

		u = 0.5 * (u / maxAxis + 1)
		v = 0.5 * (v / maxAxis + 1)
		v = 1 - v

		return map:Sample(u, v, mip, 0, face)
	end

	local function Reflect(incident, normal)
		return -incident + 2 * incident:Dot(normal) * normal
	end

	local function Refract(incident, normal, invEta)
		local cosine = normal:Dot(incident)
		local k = 1 + invEta * invEta * (cosine * cosine - 1)
		if k < 0 then return Vector(0, 0, 0) end
		return -incident * invEta + (invEta * cosine - math.sqrt(k)) * normal;
	end

	-- https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
	local function ACESFilmic(x)
		local a = 2.51
		local b = 0.03
		local c = 2.43
		local d = 0.59
		local e = 0.14

		local num = x * (a * x + Vector(b, b, b))
		local denom = x * (c * x + Vector(d, d, d)) + Vector(e, e, e)
		local fit = Vector(num[1] / denom[1], num[2] / denom[2], num[3] / denom[3])
		fit[1] = math.Clamp(fit[1], 0, 1)
		fit[2] = math.Clamp(fit[2], 0, 1)
		fit[3] = math.Clamp(fit[3], 0, 1)

		return fit
	end

	-- Preview params
	local PREVIEW_RES = 256
	local PREVIEW_PADDING = 64
	local PREVIEW_GAMMA = 1 / 2.2
	local PREVIEW_POINT_LIGHT = Vector(-4, 3, 3) -- Position of point light relative to sphere (x points into the screen, y and z are screen x and y)
	local PREVIEW_POINT_LIGHT_COLOUR = Vector(1, 1, 1)
	local PREVIEW_POINT_LIGHT_INTENSITY = 300

	-- Only used for displaying the background
	local FOCAL_LENGTH = 20
	local SENSOR_HEIGHT = 24

	local previewRT = GetRenderTarget("VisTrace.BSDFMaterialPreview", PREVIEW_RES, PREVIEW_RES)
	local previewMat = CreateMaterial("VisTrace.BSDFMaterialPreview", "UnlitGeneric", {
		["$basetexture"] = previewRT:GetName()
	})

	-- Cache padding calcs
	local RES_MINUS_PADDING = PREVIEW_RES - PREVIEW_PADDING
	local PAD_DIV_2 = PREVIEW_PADDING / 2

	-- Cache camera calcs
	local CAM_SCALE = 0.5 * SENSOR_HEIGHT / FOCAL_LENGTH

	local sampler = vistrace and vistrace.CreateSampler() or {}
	local hdri = vistrace and vistrace.LoadTexture("vistrace/material_preview/gallery002") or {}
	local hdrimips = vistrace and hdri:GetMIPLevels() or 0

	local function DrawPreview()
		if not vistrace then return end

		render.PushRenderTarget(previewRT)
		render.Clear(0, 0, 0, 255, true, true)

		local plr = LocalPlayer()

		local dielectric = Vector(
			plr:GetInfoNum("bsdf_material_dielectric_r", 255) / 255,
			plr:GetInfoNum("bsdf_material_dielectric_g", 255) / 255,
			plr:GetInfoNum("bsdf_material_dielectric_b", 255) / 255
		)

		local conductive = Vector(
			plr:GetInfoNum("bsdf_material_conductive_r", 255) / 255,
			plr:GetInfoNum("bsdf_material_conductive_g", 255) / 255,
			plr:GetInfoNum("bsdf_material_conductive_b", 255) / 255
		)

		local edgetint = Vector(
			plr:GetInfoNum("bsdf_material_edgetint_r", 255) / 255,
			plr:GetInfoNum("bsdf_material_edgetint_g", 255) / 255,
			plr:GetInfoNum("bsdf_material_edgetint_b", 255) / 255
		)
		local falloff  = plr:GetInfoNum("bsdf_material_falloff", 0.2)

		local roughness = plr:GetInfoNum("bsdf_material_roughnessoverride", 0) ~= 0 and plr:GetInfoNum("bsdf_material_roughness", 1) or 1
		local metalness = plr:GetInfoNum("bsdf_material_metalnessoverride", 0) ~= 0 and plr:GetInfoNum("bsdf_material_metalness", 0) or 0

		local anisotropy          = plr:GetInfoNum("bsdf_material_anisotropy", 0)
		local anisotropicrotation = plr:GetInfoNum("bsdf_material_anisotropicrotation", 0) / 360

		local ior       = plr:GetInfoNum("bsdf_material_ior", 1.5)
		local difftrans = plr:GetInfoNum("bsdf_material_difftrans", 0)
		local spectrans = plr:GetInfoNum("bsdf_material_spectrans", 0)
		local thin      = plr:GetInfoNum("bsdf_material_thin", 0) ~= 0

		local mat = vistrace.CreateMaterial()
		mat:DielectricColour(dielectric)
		mat:ConductorColour(conductive)

		mat:EdgeTint(edgetint)
		mat:EdgeTintFalloff(falloff)

		mat:Roughness(roughness)
		mat:Metalness(metalness)

		mat:Anisotropy(anisotropy)
		mat:AnisotropicRotation(anisotropicrotation)

		mat:IoR(ior)
		mat:DiffuseTransmission(difftrans)
		mat:SpecularTransmission(spectrans)
		mat:Thin(thin)

		local delta = (roughness * roughness) < 0.0063 -- kMinGGXAlpha from the binary (slightly lower due what i assume are precision issues)

		for y = 0, PREVIEW_RES - 1 do
			for x = 0, PREVIEW_RES - 1 do
				render.SetViewPort(x, y, 1, 1)

				local xCam = (2 * (x + 0.5) / PREVIEW_RES - 1) * CAM_SCALE
				local yCam = (1 - 2 * (y + 0.5) / PREVIEW_RES) * CAM_SCALE

				local dir = Vector(1, xCam, yCam)
				dir:Normalize()

				local u = (x - PAD_DIV_2) / RES_MINUS_PADDING
				local v = (y - PAD_DIV_2) / RES_MINUS_PADDING
				u = u * 2 - 1
				v = v * 2 - 1

				local r2 = u * u + v * v
				if r2 < 1 then
					local normal = Vector(-math.sqrt(1 - r2), u, -v)
					local tangent = Vector(0, 0, 1):Cross(normal):GetNormalized()
					local binormal = normal:Cross(tangent):GetNormalized()

					local incident = Vector(-1, 0, 0)
					iDotN = normal:Dot(incident)
					local deltaReflect = Reflect(incident, normal)
					local deltaRefract = thin and -incident or Refract(incident, normal, 1 / ior)

					-- Direct contribution
					local lDir = PREVIEW_POINT_LIGHT - normal
					local lengthSqr = lDir:LengthSqr()
					local length = math.sqrt(lengthSqr)
					lDir = lDir / length

					local Li = PREVIEW_POINT_LIGHT_COLOUR * PREVIEW_POINT_LIGHT_INTENSITY / (4 * math.pi * lengthSqr)

					mat:ActiveLobes(delta and LobeType.Diffuse or LobeType.All)
					local direct = vistrace.EvalBSDF(mat, normal, tangent, binormal, incident, lDir) * Li

					--- Indirect contribution
					mat:ActiveLobes(LobeType.DiffuseReflection)
					local diffusePdf = vistrace.EvalPDF(mat, normal, tangent, binormal, incident, normal)
					local diffuseReflection = Vector(0, 0, 0)
					if diffusePdf > 0 then
						diffuseReflection = vistrace.EvalBSDF(mat, normal, tangent, binormal, incident, normal)
							/ diffusePdf
							* DirToCubemap(hdri, normal, hdrimips - 1)
					end

					local specularReflection = Vector(0, 0, 0)
					if delta then
						mat:ActiveLobes(LobeType.DeltaSpecularReflection)
						specularReflection = vistrace.SampleBSDF(sampler, mat, normal, tangent, binormal, incident).weight * DirToCubemap(hdri, deltaReflect, 0)

						mat:ActiveLobes(LobeType.DeltaConductiveReflection)
						specularReflection = specularReflection + vistrace.SampleBSDF(sampler, mat, normal, tangent, binormal, incident).weight * DirToCubemap(hdri, deltaReflect, 0)
					else
						mat:ActiveLobes(bit.bor(LobeType.SpecularReflection, LobeType.ConductiveReflection))
						local specularReflectionPdf = vistrace.EvalPDF(mat, normal, tangent, binormal, incident, deltaReflect)
						if specularReflectionPdf > 0 then
							specularReflection = vistrace.EvalBSDF(mat, normal, tangent, binormal, incident, deltaReflect)
								/ specularReflectionPdf
								* DirToCubemap(hdri, deltaReflect, roughness * (hdrimips - 1))
						end
					end

					local specularTransmission = Vector(0, 0, 0)
					if delta then
						mat:ActiveLobes(LobeType.DeltaSpecularTransmission)
						specularTransmission = vistrace.SampleBSDF(sampler, mat, normal, tangent, binormal, incident).weight * DirToCubemap(hdri, thin and dir or deltaRefract, 0)
					else
						mat:ActiveLobes(LobeType.SpecularTransmission)
						local specularTransmissionPdf = vistrace.EvalPDF(mat, normal, tangent, binormal, incident, deltaRefract)
						if specularTransmissionPdf > 0 then
							specularTransmission = vistrace.EvalBSDF(mat, normal, tangent, binormal, incident, deltaRefract)
								/ specularTransmissionPdf
								* DirToCubemap(hdri, thin and dir or deltaRefract, roughness * (hdrimips - 1))
						end
					end

					local lighting = direct + diffuseReflection + specularReflection + specularTransmission
					lighting = ACESFilmic(lighting)

					render.Clear(
						math.Clamp(math.pow(lighting[1], PREVIEW_GAMMA), 0, 1) * 255,
						math.Clamp(math.pow(lighting[2], PREVIEW_GAMMA), 0, 1) * 255,
						math.Clamp(math.pow(lighting[3], PREVIEW_GAMMA), 0, 1) * 255,
						255, true, true
					)
				else
					local lighting = DirToCubemap(hdri, dir, 0)
					lighting = ACESFilmic(lighting)

					render.Clear(
						math.Clamp(math.pow(lighting[1], PREVIEW_GAMMA), 0, 1) * 255,
						math.Clamp(math.pow(lighting[2], PREVIEW_GAMMA), 0, 1) * 255,
						math.Clamp(math.pow(lighting[3], PREVIEW_GAMMA), 0, 1) * 255,
						255, true, true
					)
				end
			end
		end

		render.PopRenderTarget()
		hook.Remove("PostRenderVGUI", "VisTrace.BSDFMaterialPreview")
	end

	local CON_VARS_DEFAULT = TOOL:BuildConVarList()
	function TOOL.BuildCPanel(CPanel)
		CPanel:ClearControls()

		CPanel:AddControl("Header", {Description = [[
			Configure a VisTrace BSDF material and apply it to entities
			You can also preview the material below if you have the latest VisTrace binary module installed
		]]})

		CPanel:ToolPresets("BSDFMaterial", CON_VARS_DEFAULT)

		local useSeparateColours = CPanel:CheckBox("Separate dielectric and conductive colours", "bsdf_material_separatecolours")

		local dielectricMixer = vgui.Create("DColorMixer")
		local conductiveMixer = vgui.Create("DColorMixer")

		dielectricMixer:SetPalette(false)
		dielectricMixer:SetAlphaBar(false)
		dielectricMixer:SetWangs(true)
		dielectricMixer:SetColor(Color(255, 255, 255))
		dielectricMixer:SetConVarR("bsdf_material_dielectric_r")
		dielectricMixer:SetConVarG("bsdf_material_dielectric_g")
		dielectricMixer:SetConVarB("bsdf_material_dielectric_b")

		function dielectricMixer:ValueChanged(col)
			if not useSeparateColours:GetChecked() then
				conductiveMixer:SetColor(col)
			end
		end

		conductiveMixer:SetLabel("Conductor Colour")
		conductiveMixer:SetPalette(false)
		conductiveMixer:SetAlphaBar(false)
		conductiveMixer:SetWangs(true)
		conductiveMixer:SetColor(Color(255, 255, 255))
		conductiveMixer:SetConVarR("bsdf_material_conductive_r")
		conductiveMixer:SetConVarG("bsdf_material_conductive_g")
		conductiveMixer:SetConVarB("bsdf_material_conductive_b")

		function useSeparateColours:OnChange(val)
			if val then
				conductiveMixer:SetVisible(true)
				dielectricMixer:SetLabel("Dielectric Colour")
				CPanel:InvalidateChildren(true)
			else
				conductiveMixer:SetVisible(false)
				dielectricMixer:SetLabel("Colour")
				CPanel:InvalidateChildren(true)

				conductiveMixer:SetColor(dielectricMixer:GetColor())
			end
		end

		CPanel:AddItem(useSeparateColours)
		CPanel:AddItem(dielectricMixer)
		CPanel:AddItem(conductiveMixer)

		local edgetintMixer = vgui.Create("DColorMixer")
		edgetintMixer:SetLabel("Edge Tint")
		edgetintMixer:SetPalette(false)
		edgetintMixer:SetAlphaBar(false)
		edgetintMixer:SetWangs(true)
		edgetintMixer:SetColor(Color(255, 255, 255))
		edgetintMixer:SetConVarR("bsdf_material_edgetint_r")
		edgetintMixer:SetConVarG("bsdf_material_edgetint_g")
		edgetintMixer:SetConVarB("bsdf_material_edgetint_b")
		CPanel:AddItem(edgetintMixer)

		local metalnessOverride = CPanel:CheckBox("Override metalness", "bsdf_material_metalnessoverride")
		local metalness = CPanel:NumSlider("Metalness", "bsdf_material_metalness", 0, 1, 2)
		function metalnessOverride:OnChange(val)
			if val then
				metalness:SetMouseInputEnabled(true)
				metalness:SetAlpha(255)
			else
				metalness:SetMouseInputEnabled(false)
				metalness:SetAlpha(75)
			end
		end

		local roughnessOverride = CPanel:CheckBox("Override roughness", "bsdf_material_roughnessoverride")
		local roughness = CPanel:NumSlider("Roughness", "bsdf_material_roughness", 0, 1, 2)
		function roughnessOverride:OnChange(val)
			if val then
				roughness:SetMouseInputEnabled(true)
				roughness:SetAlpha(255)
			else
				roughness:SetMouseInputEnabled(false)
				roughness:SetAlpha(75)
			end
		end

		CPanel:NumSlider("Edge Tint Falloff (Use 0.2 for Fresnel)", "bsdf_material_falloff", 0, 1, 2)

		CPanel:NumSlider("Anisotropy", "bsdf_material_anisotropy", 0, 1, 2)
		CPanel:NumSlider("Anisotropic Rotation", "bsdf_material_anisotropicrotation", 0, 360, 0)

		CPanel:NumSlider("Index of Refraction", "bsdf_material_ior", 1, 5, 2)
		CPanel:NumSlider("Specular Transmission", "bsdf_material_spectrans", 0, 1, 2)
		CPanel:NumSlider("Diffuse Transmission", "bsdf_material_difftrans", 0, 1, 2)

		CPanel:CheckBox("Use thin BTDF for specular transmission", "bsdf_material_thin")

		local BUTTON_HEIGHT = 24
		local previewContainer = vgui.Create("DPanel")
		previewContainer:Dock(TOP)
		previewContainer:SetHeight(PREVIEW_RES + BUTTON_HEIGHT)
		previewContainer.Paint = nil

		local generatePreview = vgui.Create("DButton", previewContainer)
		generatePreview:SetSize(PREVIEW_RES, BUTTON_HEIGHT)
		generatePreview:SetText("Generate Preview")
		function generatePreview:DoClick()
			hook.Add("PostRenderVGUI", "VisTrace.BSDFMaterialPreview", DrawPreview)
		end

		local previewPanel = vgui.Create("DPanel", previewContainer)
		previewPanel:SetSize(PREVIEW_RES, PREVIEW_RES)

		function previewPanel:Paint(w, h)
			surface.SetDrawColor(255, 255, 255)
			surface.SetMaterial(previewMat)
			surface.DrawTexturedRect(0, 0, w, h)
		end

		function previewContainer:PerformLayout(w, h)
			local offset = w * 0.5 - PREVIEW_RES / 2
			generatePreview:SetPos(offset, 0)
			previewPanel:SetPos(offset, BUTTON_HEIGHT)
		end

		CPanel:AddPanel(previewContainer)
	end
end

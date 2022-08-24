TOOL.Category = "Render"
TOOL.Name = "#tool.bsdf_material.name"

TOOL.Information = {
	{name = "left"},
	{name = "right"},
	{name = "reload"}
}

TOOL.ClientConVar.r = 255
TOOL.ClientConVar.g = 255
TOOL.ClientConVar.b = 255
TOOL.ClientConVar.roughness = 1
TOOL.ClientConVar.metalness = 0
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
		ent:SetNWVector("BSDFMaterial.Colour", materialTbl.Colour)
		ent:SetNWFloat("BSDFMaterial.Roughness", materialTbl.Roughness)
		ent:SetNWFloat("BSDFMaterial.Metalness", materialTbl.Metalness)
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
	print("TEST")
	local ent = trace.Entity
	if IsValid(ent.AttachedEntity) then ent = ent.AttachedEntity end

	if not IsValid(ent) then return false end
	if CLIENT then return true end

	local r = self:GetClientNumber("r", 255)
	local g = self:GetClientNumber("g", 255)
	local b = self:GetClientNumber("b", 255)
	local roughness = self:GetClientNumber("roughness", 1)
	local metalness = self:GetClientNumber("metalness", 0)
	local ior = self:GetClientNumber("ior", 1.5)
	local difftrans = self:GetClientNumber("difftrans", 0)
	local spectrans = self:GetClientNumber("spectrans", 0)
	local thin = self:GetClientNumber("thin", 0) ~= 0

	SetMaterial(self:GetOwner(), ent, {
		Colour = Vector(r, g, b) / 255,
		Roughness = roughness,
		Metalness = metalness,
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
		Colour = Vector(1, 1, 1),
		Roughness = 1,
		Metalness = 0,
		IoR = 1.5,
		DiffuseTransmission = 0,
		SpecularTransmission = 0,
		Thin = false
	}

	self:GetOwner():ConCommand("bsdf_material_r "         .. mat.Colour[1] * 255)
	self:GetOwner():ConCommand("bsdf_material_g "         .. mat.Colour[2] * 255)
	self:GetOwner():ConCommand("bsdf_material_b "         .. mat.Colour[3] * 255)
	self:GetOwner():ConCommand("bsdf_material_roughness " .. mat.Roughness)
	self:GetOwner():ConCommand("bsdf_material_metalness " .. mat.Metalness)
	self:GetOwner():ConCommand("bsdf_material_ior "       .. mat.IoR)
	self:GetOwner():ConCommand("bsdf_material_difftrans " .. mat.DiffuseTransmission)
	self:GetOwner():ConCommand("bsdf_material_spectrans " .. mat.SpecularTransmission)
	self:GetOwner():ConCommand("bsdf_material_thin "      .. mat.Thin and "1" or "0")

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

		mat:Colour(self:GetNWVector("BSDFMaterial.Colour"))
		mat:Roughness(self:GetNWFloat("BSDFMaterial.Roughness"))
		mat:Metalness(self:GetNWFloat("BSDFMaterial.Metalness"))
		mat:IoR(self:GetNWFloat("BSDFMaterial.IoR"))
		mat:DiffuseTransmission(self:GetNWFloat("BSDFMaterial.DiffuseTransmission"))
		mat:SpecularTransmission(self:GetNWFloat("BSDFMaterial.SpecularTransmission"))
		mat:Thin(self:GetNWBool("BSDFMaterial.Thin"))

		return mat
	end

	local CON_VARS_DEFAULT = TOOL:BuildConVarList()
	function TOOL.BuildCPanel(CPanel)
		CPanel:ClearControls()

		CPanel:AddControl("Header", {Description = [[
			Configure a VisTrace BSDF material and apply it to entities
			You can also preview the material below if you have the latest VisTrace binary module installed
		]]})

		CPanel:ToolPresets("BSDFMaterial", CON_VARS_DEFAULT)

		local width = CPanel:GetWide()

		local diffuseMixer = vgui.Create("DColorMixer")
		diffuseMixer:SetLabel("Diffuse Colour")
		diffuseMixer:Dock(FILL)
		diffuseMixer:SetPalette(true)
		diffuseMixer:SetAlphaBar(false)
		diffuseMixer:SetWangs(true)
		diffuseMixer:SetColor(Color(255, 255, 255))
		diffuseMixer:SetConVarR("bsdf_material_r")
		diffuseMixer:SetConVarG("bsdf_material_g")
		diffuseMixer:SetConVarB("bsdf_material_b")
		diffuseMixer:SetSize(1, 200)
		CPanel:AddItem(diffuseMixer)

		CPanel:NumSlider("Metalness", "bsdf_material_metalness", 0, 1, 2)
		CPanel:NumSlider("Roughness", "bsdf_material_roughness", 0, 1, 2)
		CPanel:NumSlider("Index of Refraction", "bsdf_material_ior", 1, 5, 2)
		CPanel:NumSlider("Specular Transmission", "bsdf_material_spectrans", 0, 1, 2)
		CPanel:NumSlider("Diffuse Transmission", "bsdf_material_difftrans", 0, 1, 2)
	end
end

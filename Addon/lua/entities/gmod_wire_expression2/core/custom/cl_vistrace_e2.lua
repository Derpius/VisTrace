--[[
	Function Caching
]]

local net_Start, net_SendToServer = net.Start, net.SendToServer
local net_ReadBool, net_ReadUInt, net_ReadFloat = net.ReadBool, net.ReadUInt, net.ReadFloat
local net_WriteBool, net_WriteUInt, net_WriteFloat = net.WriteBool, net.WriteUInt, net.WriteFloat

local ents_GetAll = ents.GetAll
local _ipairs = ipairs
local _Vector = Vector

local createAccel
if vistrace then createAccel = vistrace.CreateAccel end

local accels = {}

net.Receive("VisTrace.E2.Build", function()
	if not vistrace then return end
	local chip = net_ReadUInt(12)

	if not accels[chip] then accels[chip] = createAccel() end

	local valid = {}
	local numValid = 0
	local result = ents_GetAll()
	for k, v in _ipairs(result) do
		local cls = v:GetClass()
		if cls == "prop_physics" or cls == "prop_ragdoll" then
			numValid = numValid + 1
			valid[numValid] = v
		end
	end
	accels[chip]:Rebuild(valid)
end)
net.Receive("VisTrace.E2.Destruct", function()
	accels[net_ReadUInt(12)] = nil
end)

net.Receive("VisTrace.E2.Net", function()
	-- Read the header
	local hitWorld, hitWater = net_ReadBool(), net_ReadBool()
	local chip = net_ReadUInt(12)
	local count = net_ReadUInt(11)

	-- Check the module is installed
	if not vistrace then
		net_Start("VisTrace.E2.Net")
		net_WriteUInt(chip, 12)
		net_WriteBool(false)
		net_SendToServer()
		return
	end

	local results = {}
	for i = 1, count do
		results[i] = accels[chip]:Traverse(
			_Vector(net_ReadFloat(), net_ReadFloat(), net_ReadFloat()),
			_Vector(net_ReadFloat(), net_ReadFloat(), net_ReadFloat()),
			nil, nil,
			hitWorld, hitWater
		)
	end

	net_Start("VisTrace.E2.Net")
	net_WriteUInt(chip, 12)
	net_WriteBool(true)
	net_WriteUInt(count, 11)
	for i = 1, count do
		local result = results[i]
		if not result.Hit or result.HitWorld then
			net_WriteBool(false)
		else
			net_WriteBool(true)
			net_WriteUInt(result.EntIndex, 12)

			for j = 1, 3 do net_WriteFloat(result.HitPos[j]) end
			for j = 1, 3 do net_WriteFloat(result.HitNormal[j]) end
			for j = 1, 3 do net_WriteFloat(result.HitTangent[j]) end
			for j = 1, 3 do net_WriteFloat(result.HitNormalGeometric[j]) end

			net_WriteFloat(result.HitTexCoord.u)
			net_WriteFloat(result.HitTexCoord.v)
			net_WriteUInt(result.SubmatIndex, 5)
		end
	end
	net_SendToServer()
end)

--[[
	E2 Helper Docs
]]

local tbl = E2Helper.Descriptions
local function desc(name, description)
    tbl[name] = description
end

desc("vtBufferMutable()", "Returns whether you can currently add new rays to the buffer")
desc("vtBufferCap()", "Returns the maximum number of rays you can add to the buffer")
desc("vtBufferRay(vv)", "Adds a ray to the buffer")

desc("vtSendBuffer()", "Send the current ray buffer")
desc("vtSendBuffer(n)", "Send the current ray buffer")
desc("vtSendBuffer(nn)", "Send the current ray buffer")

desc("vtReceiveClk()", "Returns true if the chip was executed by a result netmsg")
desc("vtReadResult()", [[
Reads a result from the buffer as a table with the following keys:
Hit: number (if 0 no other keys will be present)
Entity: entity
HitPos: vector
HitNormal: vector
HitTangent: vector
HitBinormal: vector
HitNormalGeometric: vector (the normal of the tri hit, without smoothed vertex normals)
HitU: number
HitV: number
SubmatIndex: number (0 indexed id of the submaterial hit)
]])

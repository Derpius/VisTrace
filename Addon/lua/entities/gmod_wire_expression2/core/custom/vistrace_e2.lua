E2Lib.RegisterExtension(
	"vistrace",
	true,
	"Constructs and traverses a BVH acceleration structure on the CPU allowing for high speed vismesh intersections\nRequires the binary module installed to use, which you can get here https://github.com/Derpius/VisTrace/releases"
)

util.AddNetworkString("VisTrace.E2.Net")
util.AddNetworkString("VisTrace.E2.Build")
util.AddNetworkString("VisTrace.E2.Destruct")

local RAY_STRIDE = 6
local BUFFER_MAX = CreateConVar("vistrace_e2_buffer_max", 256, FCVAR_ARCHIVE, "Maximum number of rays/results in a single buffer", 0, 1024)
local NET_MSG_RATE = CreateConVar("vistrace_e2_netmsg_rate", 0.1, FCVAR_ARCHIVE, "Minimum time before sending another buffer", 0.01)

--[[
	Function Caching
]]

local net_Start, net_Send = net.Start, net.Send
local net_ReadBool, net_ReadUInt, net_ReadFloat = net.ReadBool, net.ReadUInt, net.ReadFloat
local net_WriteBool, net_WriteUInt, net_WriteFloat = net.WriteBool, net.WriteUInt, net.WriteFloat

local newE2Table = E2Lib.newE2Table

local table_remove = table.remove
local _CurTime, _unpack, _error = CurTime, unpack, error

local _Entity, _Vector = Entity, Vector

--[[
	Net Message Scheduler
]]
local rayBufferNetQueue = {}
local rayBufferNetQueueLength = 0
local lastSendTime = 0
local waitingForResponse = false
hook.Add("Think", "VisTrace.E2.NetScheduler", function()
	if waitingForResponse then return end

	local msg = rayBufferNetQueue[1]
	local curTime = _CurTime()
	if msg and curTime - lastSendTime > NET_MSG_RATE:GetFloat() then
		waitingForResponse = true
		lastSendTime = curTime

		net_Start("VisTrace.E2.Net")
		net_WriteBool(msg.hitWorld)
		net_WriteBool(msg.hitWater)
		net_WriteUInt(msg.chip.entity:EntIndex(), 12)
		net_WriteUInt(msg.numRays, 11)
		for i = 1, msg.numRays * RAY_STRIDE do
			net_WriteFloat(msg.buffer[i])
		end

		msg.sent = true
		net_Send(msg.chip.player)
	end
end)
net.Receive("VisTrace.E2.Net", function(len, plr)
	local chip = _Entity(net_ReadUInt(12))
	if not chip:IsValid() or not chip:GetClass() == "gmod_wire_expression2" or chip.player ~= plr then return end

	if not rayBufferNetQueue[1].sent then
		waitingForResponse = false
		return
	end

	if net_ReadBool() then
		local numResults = net_ReadUInt(11)

		local instance = chip.vistrace
		for i = 1, numResults do
			local e2tbl = newE2Table()
			if not net_ReadBool() then
				e2tbl.size = 1
				e2tbl.n.Hit = 0
				e2tbl.ntypes.Hit = "n"
			else
				e2tbl.size = 10

				e2tbl.s.Hit = 1
				e2tbl.stypes.Hit = "n"

				e2tbl.s.Entity = _Entity(net_ReadUInt(12))
				e2tbl.stypes.Entity = "e"

				e2tbl.s.HitPos = {
					net_ReadFloat(),
					net_ReadFloat(),
					net_ReadFloat()
				}
				e2tbl.stypes.HitPos = "v"

				e2tbl.s.HitNormal = {
					net_ReadFloat(),
					net_ReadFloat(),
					net_ReadFloat()
				}
				e2tbl.stypes.HitNormal = "v"

				e2tbl.s.HitTangent = {
					net_ReadFloat(),
					net_ReadFloat(),
					net_ReadFloat()
				}
				e2tbl.stypes.HitTangent = "v"

				e2tbl.s.HitNormalGeometric = {
					net_ReadFloat(),
					net_ReadFloat(),
					net_ReadFloat()
				}
				e2tbl.stypes.HitNormalGeometric = "v"

				e2tbl.s.HitU = net_ReadFloat()
				e2tbl.s.HitV = net_ReadFloat()
				e2tbl.stypes.HitU = "n"
				e2tbl.stypes.HitV = "n"

				e2tbl.s.SubmatIndex = net_ReadUInt(5)
				e2tbl.stypes.SubmatIndex = "n"

				local binormal = _Vector(_unpack(e2tbl.s.HitNormal)):Cross(_Vector(_unpack(e2tbl.s.HitTangent))):GetNormalized()
				e2tbl.s.HitBinormal = { binormal[1], binormal[2], binormal[3] }
				e2tbl.stypes.HitBinormal = "v"
			end
			instance.resultBuffer[i] = e2tbl
		end
		instance.numResults = numResults
	else
		chip.vistrace.notInstalled = true
	end

	chip.vistrace.vtReceiveClk = true
	chip:Execute()
	chip.vistrace.vtReceiveClk = nil

	table_remove(rayBufferNetQueue, 1)
	waitingForResponse = false
	rayBufferNetQueueLength = rayBufferNetQueueLength - 1
end)

local function canBuffer(instance)
	return not instance.activeNetMsg and instance.numRays < BUFFER_MAX:GetInt()
end

registerCallback("construct", function(self)
	self.entity.vistrace = {
		numRays = 0, -- No need to use the length operator if we just track the count live
		rayBuffer = {},
		numResults = 0,
		resultBuffer = {},
		activeNetMsg = false
	}

	net_Start("VisTrace.E2.Build")
	net_WriteUInt(self.entity:EntIndex(), 12)
	net_Send(self.player)
end)

registerCallback("destruct", function(self)
	local i = 1
	while i <= rayBufferNetQueueLength do
		if rayBufferNetQueue[i].chip.entity == self.entity then
			table_remove(rayBufferNetQueue, i)
			rayBufferNetQueueLength = rayBufferNetQueueLength - 1
		else
			i = i + 1
		end
	end

	net_Start("VisTrace.E2.Destruct")
	net_WriteUInt(self.entity:EntIndex(), 12)
	net_Send(self.player)
end)

__e2setcost(1)
e2function void vtRebuildAccel()
	net_Start("VisTrace.E2.Build")
	net_WriteUInt(self.entity:EntIndex(), 12)
	net_Send(self.player)
end

__e2setcost(1)
e2function number vtBufferMutable()
	return canBuffer(self.entity.vistrace) and 1 or 0
end

__e2setcost(1)
e2function number vtBufferCap()
	return BUFFER_MAX:GetInt()
end

__e2setcost(5)
e2function void vtBufferRay(vector origin, vector direction)
	local instance = self.entity.vistrace
	if not canBuffer(instance) then _error("Unable to buffer ray") end

	local offset = instance.numRays * RAY_STRIDE
	local buffer = instance.rayBuffer
	buffer[offset + 1] = origin[1]
	buffer[offset + 2] = origin[2]
	buffer[offset + 3] = origin[3]
	buffer[offset + 4] = direction[1]
	buffer[offset + 5] = direction[2]
	buffer[offset + 6] = direction[3]
	instance.numRays = instance.numRays + 1
end

local function sendBuffer(self, hitWorld, hitWater)
	local instance = self.entity.vistrace
	if instance.activeNetMsg then _error("Tried to send a buffer while there's an outstanding netmsg") end
	instance.activeNetMsg = true
	instance.numResults = 0
	instance.resultBuffer = {}

	rayBufferNetQueueLength = rayBufferNetQueueLength + 1
	rayBufferNetQueue[rayBufferNetQueueLength] = {
		chip = self,
		numRays = instance.numRays,
		buffer = instance.rayBuffer,
		hitWorld = hitWorld,
		hitWater = hitWater
	}
end

__e2setcost(20)
e2function void vtSendBuffer()
	if not self.player:IsValid() then _error("Tried to send VisTrace buffer to an invalid player") end
	sendBuffer(self, true, false)
end
e2function void vtSendBuffer(number hitWorld)
	sendBuffer(self, hitWorld ~= 0, false)
end
e2function void vtSendBuffer(number hitWorld, number hitWater)
	sendBuffer(self, hitWorld ~= 0, hitWater ~= 0)
end

__e2setcost(1)
e2function number vtReceiveClk()
	local instance = self.entity.vistrace
	if instance.vtReceiveClk then
		if instance.notInstalled then
			_error("A VisTrace binary module version compatible with the server's addon version (v0.4.x) is not installed")
		end
		return 1
	end
	return 0
end

__e2setcost(5)
e2function table vtReadResult()
	local instance = self.entity.vistrace
	if instance.numResults == 0 then _error("Tried to read from an empty result buffer") end

	local result = table_remove(instance.resultBuffer, 1)
	instance.numResults = instance.numResults - 1
	if instance.numResults == 0 then
		instance.numRays = 0
		instance.rayBuffer = {}
		instance.activeNetMsg = false
	end
	return result
end

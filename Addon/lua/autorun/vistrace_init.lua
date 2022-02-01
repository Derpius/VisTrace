if SERVER then
	AddCSLuaFile()
	return
end

--[[
	https://github.com/Derpius/VisTrace/issues/13
	pcall(require, "VisTrace-v0.4") -- Don't throw an error if the module failed to load
]]

local VERSION = "0.6"

local files = file.Find("lua/bin/gmcl_VisTrace-v" .. VERSION .. "_*.dll", "GAME")
if not files or #files == 0 then return end

if not system.IsWindows() then
	error("VisTrace is currently only compatible with Windows, if you'd like a cross platform build to be worked on, please open an issue at https://github.com/Derpius/VisTrace/issues")
end

if jit.arch ~= "x64" then
	error("VisTrace does not work on 32 bit builds of Garry's Mod at this time, please switch to the x86-64 branch in Steam betas")
end

require("VisTrace-v" .. VERSION)

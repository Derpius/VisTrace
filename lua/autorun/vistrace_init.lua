VISTRACE_VERSION = "0.12"

if SERVER then
	AddCSLuaFile()

	CreateConVar(
		"vistrace_version", VISTRACE_VERSION, FCVAR_NOTIFY,
		"The VisTrace addon's API version. This is used to find VisTrace servers from the master server and should not be edited"
	)

	return
end

--[[
	https://github.com/Derpius/VisTrace/issues/13
	pcall(require, "VisTrace-v0.4") -- Don't throw an error if the module failed to load
]]

local files = file.Find("lua/bin/gmcl_VisTrace-v" .. VISTRACE_VERSION .. "_*.dll", "GAME")
if not files or #files == 0 then return end

if not system.IsWindows() then
	error("VisTrace is currently only compatible with Windows, if you'd like a cross platform build to be worked on, please open an issue at https://github.com/Derpius/VisTrace/issues")
end

require("VisTrace-v" .. VISTRACE_VERSION)

file.CreateDir("vistrace")
file.CreateDir("vistrace_hdris")

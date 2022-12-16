VISTRACE_VERSION = "0.13"

if SERVER then
	AddCSLuaFile()

	CreateConVar(
		"vistrace_version",
		VISTRACE_VERSION,
		FCVAR_NOTIFY,
		"The VisTrace addon's API version. This is used to find VisTrace servers from the master server and should not be edited"
	)

	return
end

local suffix = "win" .. (jit.arch == "x86" and "32" or "64")
if system.IsLinux() then
	suffix = "linux" .. (jit.arch == "x86" and "" or "64")
elseif system.IsOSX() then
	suffix = "osx"
end

if
	not file.Exists(
		string.format(
			"lua/bin/gmcl_VisTrace-v%s_%s.dll",
			VISTRACE_VERSION,
			suffix
		),
		"GAME"
	)
then
	print(
		"The VisTrace binary module is not installed for this GMod architecture or your operating system\nGet it here: https://www.github.com/Derpius/VisTrace"
	)

	return
end

require("VisTrace-v" .. VISTRACE_VERSION)

file.CreateDir("vistrace")
file.CreateDir("vistrace_hdris")
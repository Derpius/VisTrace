if SERVER then
	AddCSLuaFile()
	return
end

pcall(require, "VisTrace") -- Don't throw an error if the module failed to load

-- Version of the binary this instance of the addon code expects (prevents mismatch between binary and the code here that uses it)
if vistrace then vistrace.AddonVersion = "v0.3.0" end
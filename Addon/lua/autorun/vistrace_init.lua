if SERVER then
	AddCSLuaFile()
	return
end

pcall(require, "VisTrace-v0.4") -- Don't throw an error if the module failed to load

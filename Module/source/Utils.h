#pragma once

#include "GarrysMod/Lua/Interface.h"
#include "glm/glm.hpp"
#include "VTFParser.h"

#include <string>
#include <vector>

// Print a string to console using Lua
void printLua(GarrysMod::Lua::ILuaBase* LUA, const char* text);

// Prints the type of every element on the stack to the console
void dumpStack(GarrysMod::Lua::ILuaBase* LUA);

/// <summary>
/// Constructs a GMod vector from a scalar
/// </summary>
/// <param name="n">Scalar</param>
/// <returns>Constructed vector</returns>
Vector MakeVector(const float n);

/// <summary>
/// Constructs a GMod vector from 3 scalar components
/// </summary>
/// <param name="x">X component</param>
/// <param name="y">Y component</param>
/// <param name="z">Z component</param>
/// <returns>Constructed vector</returns>
Vector MakeVector(const float x, const float y, const float z);

/// <summary>
/// Gets a string from the material at the top of the stack
/// </summary>
/// <param name="LUA">ILuaBase pointer</param>
/// <param name="key">String key</param>
/// <returns>Value at the key or an empty string</returns>
std::string getMaterialString(GarrysMod::Lua::ILuaBase* LUA, const std::string& key);

/// <summary>
/// VMT flags
/// </summary>
enum class MaterialFlags
{
	debug = 1,
	no_fullbright = 2,
	no_draw = 4,
	use_in_fillrate_mode = 8,
	vertexcolor = 16,
	vertexalpha = 32,
	selfillum = 64,
	additive = 128,
	alphatest = 256,
	multipass = 512,
	znearer = 1024,
	model = 2048,
	flat = 4096,
	nocull = 8192,
	nofog = 16384,
	ignorez = 32768,
	decal = 65536,
	envmapsphere = 131072,
	noalphamod = 262144,
	envmapcameraspace = 524288,
	basealphaenvmapmask = 1048576,
	translucent = 2097152,
	normalmapalphaenvmapmask = 4194304,
	softwareskin = 8388608,
	opaquetexture = 16777216,
	envmapmode = 33554432,
	nodecal = 67108864,
	halflambert = 134217728,
	wireframe = 268435456,
	allowalphatocoverage = 536870912
};
inline MaterialFlags operator|(MaterialFlags a, MaterialFlags b)
{
	return static_cast<MaterialFlags>(static_cast<unsigned int>(a) | static_cast<unsigned int>(b));
}

/// <summary>
/// Returns true if the material at the top of the stack has the flag(s) specified
/// </summary>
/// <param name="LUA">ILuaBase pointer</param>
/// <param name="flags">Flags to check</param>
/// <returns>True if the material has all of the flags specified</returns>
bool checkMaterialFlags(GarrysMod::Lua::ILuaBase* LUA, const MaterialFlags flags);

/// <summary>
/// Skins a vertex to its bones
/// </summary>
/// <param name="vec">Vector to transform</param>
/// <param name="bones">Bone list</param>
/// <param name="binds">Binds list</param>
/// <param name="weights">Bone vertex weights</param>
/// <param name="angleOnly">Whether to just transform the angle of the input vector</param>
/// <returns>The transformed vector</returns>
glm::vec3 transformToBone(
	const Vector& vec,
	const std::vector<glm::mat4>& bones, const std::vector<glm::mat4>& binds,
	const std::vector<std::pair<size_t, float>>& weights,
	const bool angleOnly = false
);

/// <summary>
/// Read a VTF texture at the given path
/// </summary>
/// <param name="path">Path to texture (without materials/ and .vtf)</param>
/// <param name="ppTextureOut">Pointer to pointer to texture for passing the newly read texture out</param>
/// <returns>Whether the read was successful or not</returns>
bool readTexture(const std::string& path, VTFTexture** ppTextureOut);

/// <summary>
/// Check if a vector is valid (not all zero or NaN)
/// </summary>
/// <param name="v">Vector to validate</param>
/// <returns>Whether the vector was valid</returns>
bool validVector(const glm::vec3& v);

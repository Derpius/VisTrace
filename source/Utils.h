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
std::string GetMaterialString(GarrysMod::Lua::ILuaBase* LUA, const std::string& key);

/// <summary>
/// VMT flags
/// </summary>
enum class MaterialFlags : uint32_t
{
	NONE = 0,
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
inline MaterialFlags operator|(const MaterialFlags a, const MaterialFlags b)
{
	return static_cast<MaterialFlags>(static_cast<const uint32_t>(a) | static_cast<const uint32_t>(b));
}
inline MaterialFlags operator&(const MaterialFlags a, const MaterialFlags b)
{
	return static_cast<MaterialFlags>(static_cast<const uint32_t>(a) & static_cast<const uint32_t>(b));
}

/// <summary>
/// Read a VTF texture at the given path
/// </summary>
/// <param name="path">Path to texture (without materials/ and .vtf)</param>
/// <param name="ppTextureOut">Pointer to pointer to texture for passing the newly read texture out</param>
/// <returns>Whether the read was successful or not</returns>
bool ReadTexture(const std::string& path, VTFTexture** ppTextureOut);

/// <summary>
/// Check if a vector is valid (not all zero or NaN)
/// </summary>
/// <param name="v">Vector to validate</param>
/// <returns>Whether the vector was valid</returns>
bool ValidVector(const glm::vec3& v);

/// <summary>
/// Transforms a texture coordinate with a material transform matrix and scale
/// </summary>
/// <param name="texcoord">UV coordinates to transform</param>
/// <param name="transform">Transformation matrix from the material</param>
/// <param name="scale">Scale value from the material (e.g. seamless_scale)</param>
/// <returns>Transformed texcoord</returns>
inline glm::vec2 TransformTexcoord(const glm::vec2& texcoord, const glm::mat2x4& transform, const float scale)
{
	glm::vec2 transformed;
	transformed.x = glm::dot(glm::vec4(texcoord, 1.f, 1.f), transform[0]);
	transformed.y = glm::dot(glm::vec4(texcoord, 1.f, 1.f), transform[1]);

	return transformed * scale;
}

// Ray Tracing Gems
inline float TriUVInfoToTexLOD(const VTFTexture* pTex, glm::vec2 uvInfo)
{
	return uvInfo.x + 0.5f * log2(pTex->GetWidth() * pTex->GetHeight() * uvInfo.y);
}

#pragma once

#include "GarrysMod/Lua/Interface.h"
#include "glm/glm.hpp"

#include "vistrace/IVTFTexture.h"
#include "SourceTypes.h"

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
/// Gets a string from the material
/// </summary>
/// <param name="mat">Material pointer</param>
/// <param name="key">String key</param>
/// <returns>Value at the key or an empty string</returns>
std::string GetMaterialString(IMaterial* mat, const std::string& key);

/// <summary>
/// Gets a material variable from the given IMaterial
/// The reason for this function existing is to handle the extra "found" variable in ->FindVar and to support std::string
/// </summary>
/// <param name="mat">Material pointer</param>
/// <param name="key">String key</param>
/// <returns>Pointer to the IMaterialVar or a nullptr</returns>
IMaterialVar* GetMaterialVar(IMaterial* mat, const std::string& key);

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
inline float TriUVInfoToTexLOD(const VisTrace::IVTFTexture* pTex, glm::vec2 uvInfo)
{
	return uvInfo.x + 0.5f * log2(pTex->GetWidth() * pTex->GetHeight() * uvInfo.y);
}

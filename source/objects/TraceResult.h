#pragma once

#include <cstdint>
#include "glm/glm.hpp"

#include "BSPParser.h"

#include "AccelStruct.h"
#include "vistrace/IVTFTexture.h"

#include "Utils.h"

class TraceResult
{
private:
	Material material;

	bool blendFactorSet = false;
	float blendFactor;

	glm::vec3 v[3];
	glm::vec3 vN[3];
	glm::vec3 vT[3];
	glm::vec3 vB[3];
	glm::vec2 vUV[3];

	bool posSet = false;
	glm::vec3 pos;

	glm::vec3 geometricNormalWorld;

	bool tbnSet = false;
	glm::vec3 normal;
	glm::vec3 tangent;
	glm::vec3 binormal;

	bool mipOverride;
	float coneWidth;
	float coneAngle;
	bool textureLodSet = false;
	glm::vec2 textureLodInfo;

	bool shadingDataSet = false;
	glm::vec3 albedo;
	float alpha;
	float metalness = 0;
	float roughness = 1;

	void CalcFootprint();
	void CalcBlendFactor();
	void CalcTBN();
	void CalcShadingData();

public:
	static int id;

	float distance;
	glm::vec3 wo;

	glm::vec3 geometricNormal;

	glm::vec3 uvw;
	glm::vec2 texUV;
	float lodOffset;

	uint32_t entIdx;
	CBaseEntity* rawEnt;
	uint32_t submatIdx;

	bool hitSky = false;

	bool frontFacing;

	TraceResult(
		const glm::vec3& direction, float distance,
		float coneWidth, float coneAngle,
		const Triangle& tri, const TriangleData& triData,
		const glm::vec2& uv,
		const Entity& ent, const Material mat
	);

	const glm::vec3& GetPos();

	const glm::vec3& GetNormal();
	const glm::vec3& GetTangent();
	const glm::vec3& GetBinormal();

	const glm::vec3& GetAlbedo();
	float GetAlpha();

	float GetMetalness();
	float GetRoughness();

	float GetBaseMIPLevel();

	std::string GetMaterial() const;
	MaterialFlags GetMaterialFlags() const;
	BSPEnums::SURF GetSurfFlags() const;
	bool HitWater() const;

	std::string GetBaseTexture() const;
	std::string GetNormalMap() const;
	std::string GetMRAO() const;

	std::string GetBaseTexture2() const;
	std::string GetNormalMap2() const;
	std::string GetMRAO2() const;

	std::string GetBlendTexture() const;
	std::string GetDetailTexture() const;
};

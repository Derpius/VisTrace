#pragma once

#include <cstdint>
#include "glm/glm.hpp"

#include "VTFParser.h"
#include "BSPParser.h"

#include "AccelStruct.h"

#include "Utils.h"

class TraceResult
{
private:
	VTFTexture* baseTexture;
	VTFTexture* normalMap;
	VTFTexture* mrao;

	glm::vec3 vN[3];
	glm::vec3 vT[3];
	glm::vec3 vB[3];
	glm::vec2 vUV[3];

	bool tbnSet = false;
	glm::vec3 normal;
	glm::vec3 tangent;
	glm::vec3 binormal;

	bool shadingDataSet = false;
	glm::vec3 albedo;
	float alpha;
	float metalness = 0;
	float roughness = 1;

	void CalcTBN();
	void CalcShadingData();

public:
	static int id;

	glm::vec3 pos;
	glm::vec3 wo;
	glm::vec3 geometricNormal;

	glm::vec3 uvw;
	glm::vec2 texUV;

	uint32_t entIdx;
	CBaseEntity* rawEnt;
	uint32_t submatIdx;

	MaterialFlags materialFlags;
	BSPEnums::SURF surfaceFlags;

	TraceResult(
		const glm::vec3& direction,
		const Triangle& tri, const TriangleData& triData,
		const glm::vec2& uv,
		const Entity& ent,
		MaterialFlags materialFlags, BSPEnums::SURF surfaceFlags,
		VTFTexture* baseTexture, VTFTexture* normalMap = nullptr, VTFTexture* mrao = nullptr
	);

	const glm::vec3& GetNormal();
	const glm::vec3& GetTangent();
	const glm::vec3& GetBinormal();

	const glm::vec3& GetAlbedo();
	float GetAlpha();

	bool HasPBRData();
	float GetMetalness();
	float GetRoughness();
};

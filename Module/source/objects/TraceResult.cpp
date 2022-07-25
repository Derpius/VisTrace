#include "TraceResult.h"

int TraceResult::id = -1;

TraceResult::TraceResult(
	const glm::vec3& direction,
	const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
	const glm::vec3 vertNormals[3], const glm::vec3 vertTangents[3], const glm::vec3 vertBinormals[3], const glm::vec2 vertUVs[3],
	const glm::vec3& geometricNormal, const glm::vec2& uv,
	uint32_t entIdx, CBaseEntity* rawEnt, uint32_t submatIdx,
	uint32_t materialFlags, const glm::vec4& entColour,
	VTFTexture* baseTexture, VTFTexture* normalMap, VTFTexture* mrao
) :
	geometricNormal(geometricNormal),
	entIdx(entIdx), rawEnt(rawEnt), submatIdx(submatIdx),
	materialFlags(materialFlags),
	baseTexture(baseTexture), normalMap(normalMap), mrao(mrao)
{
	wo = -direction;

	for (int i = 0; i < 3; i++) {
		vN[i] = vertNormals[i];
		vT[i] = vertTangents[i];
		vB[i] = vertBinormals[i];
		vUV[i] = vertUVs[i];
	}

	uvw = glm::vec3(uv, 1.f - uv[0] - uv[1]);
	pos = uvw[2] * v0 + uvw[0] * v1 + uvw[1] * v2;

	texUV = uvw[2] * vUV[0] + uvw[0] * vUV[1] + uvw[1] * vUV[2];
	texUV -= glm::floor(texUV);

	albedo = entColour;
	alpha = entColour[3];
}

void TraceResult::CalcTBN()
{
	if (tbnSet) return;

	normal = uvw[2] * vN[0] + uvw[0] * vN[1] + uvw[1] * vN[2];
	tangent = uvw[2] * vT[0] + uvw[0] * vT[1] + uvw[1] * vT[2];
	binormal = uvw[2] * vB[0] + uvw[0] * vB[1] + uvw[1] * vB[2];

	if (normalMap != nullptr) {
		VTFPixel pixelNormal = normalMap->GetPixel(
			floor(texUV.x * normalMap->GetWidth()),
			floor(texUV.y * normalMap->GetHeight()),
			0
		);

		normal = glm::mat3{
			tangent[0],  tangent[1],  tangent[2],
			binormal[0], binormal[1], binormal[2],
			normal[0],   normal[1],   normal[2]
		} *(glm::vec3{ pixelNormal.r, pixelNormal.g, pixelNormal.b } *2.f - 1.f);
		normal = glm::normalize(normal);

		tangent = glm::normalize(tangent - normal * glm::dot(tangent, normal));
		binormal = -glm::cross(normal, tangent);
	}

	if (glm::dot(geometricNormal, wo) < 0.f) {
		normal = -normal;
		tangent = -tangent;
		binormal = -binormal;
	}

	tbnSet = true;
}

void TraceResult::CalcShadingData()
{
	if (shadingDataSet) return;

	VTFPixel colour = baseTexture->GetPixel(
		floor(texUV.x * baseTexture->GetWidth()),
		floor(texUV.y * baseTexture->GetHeight()),
		0
	);

	albedo *= glm::vec3(colour.r, colour.g, colour.b);
	alpha *= colour.a;

	if (mrao != nullptr) {
		VTFPixel metalnessRoughness = mrao->GetPixel(
			floor(texUV.x * mrao->GetWidth()),
			floor(texUV.y * mrao->GetHeight()),
			0
		);

		metalness = metalnessRoughness.r;
		roughness = metalnessRoughness.g;
	}

	shadingDataSet = true;
}

const glm::vec3& TraceResult::GetNormal()
{
	CalcTBN();
	return normal;
}
const glm::vec3& TraceResult::GetTangent()
{
	CalcTBN();
	return tangent;
}
const glm::vec3& TraceResult::GetBinormal()
{
	CalcTBN();
	return binormal;
}

const glm::vec3& TraceResult::GetAlbedo()
{
	CalcShadingData();
	return albedo;
}
float TraceResult::GetAlpha()
{
	CalcShadingData();
	return alpha;
}

bool TraceResult::HasPBRData()
{
	return mrao != nullptr;
}
float TraceResult::GetMetalness()
{
	return metalness;
}
float TraceResult::GetRoughness()
{
	return roughness;
}

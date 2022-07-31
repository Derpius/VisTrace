#include "TraceResult.h"

int TraceResult::id = -1;

TraceResult::TraceResult(
	const glm::vec3& direction,
	const Triangle& tri, const TriangleData& triData,
	const glm::vec2& uv,
	const Entity& ent,
	MaterialFlags materialFlags, BSPEnums::SURF surfaceFlags,
	VTFTexture* baseTexture, VTFTexture* normalMap, VTFTexture* mrao
) :
	materialFlags(materialFlags), surfaceFlags(surfaceFlags),
	baseTexture(baseTexture), normalMap(normalMap), mrao(mrao)
{
	wo = -direction;

	for (int i = 0; i < 3; i++) {
		vN[i] = triData.normals[i];
		vT[i] = triData.tangents[i];
		vB[i] = triData.binormals[i];
		vUV[i] = triData.uvs[i];
	}

	v[0] = glm::vec3(tri.p0[0], tri.p0[1], tri.p0[2]);
	Vector3 p1 = tri.p1(), p2 = tri.p2();
	v[1] = glm::vec3(p1[0], p1[1], p1[2]);
	v[2] = glm::vec3(p2[0], p2[1], p2[2]);

	uvw = glm::vec3(uv, 1.f - uv[0] - uv[1]);
	geometricNormal = glm::vec3(tri.n[0], tri.n[1], tri.n[2]);

	entIdx = ent.id;
	rawEnt = ent.rawEntity;
	submatIdx = triData.submatIdx;

	albedo = ent.colour;
	alpha = ent.colour.a;
}

void TraceResult::CalcTexCoord()
{
	if (texUVSet) return;

	texUV = uvw[2] * vUV[0] + uvw[0] * vUV[1] + uvw[1] * vUV[2];
	texUV -= glm::floor(texUV);

	texUVSet = true;
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
	CalcTexCoord();

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

const glm::vec3& TraceResult::GetPos()
{
	if (!posSet) {
		pos = uvw[2] * v[0] + uvw[0] * v[1] + uvw[1] * v[2];
		posSet = true;
	}
	return pos;
}

const glm::vec3& TraceResult::GetGeometricNormal()
{
	if (!geoNormSet) {
		geometricNormal = glm::normalize(geometricNormal);
		geoNormSet = true;
	}
	return geometricNormal;
}

const glm::vec2& TraceResult::GetTexCoord()
{
	CalcTexCoord();
	return texUV;
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

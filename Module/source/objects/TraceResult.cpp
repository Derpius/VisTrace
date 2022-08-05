#include "TraceResult.h"

#include "glm/gtx/compatibility.hpp"

int TraceResult::id = -1;

TraceResult::TraceResult(
	const glm::vec3& direction,
	const Triangle& tri, const TriangleData& triData,
	const glm::vec2& uv,
	const Entity& ent,
	MaterialFlags materialFlags, BSPEnums::SURF surfaceFlags, bool maskedBlending,
	VTFTexture* baseTexture, VTFTexture* normalMap, VTFTexture* mrao,
	VTFTexture* baseTexture2, VTFTexture* normalMap2, VTFTexture* mrao2,
	VTFTexture* blendTexture
) :
	materialFlags(materialFlags), surfaceFlags(surfaceFlags), maskedBlending(maskedBlending),
	baseTexture(baseTexture), normalMap(normalMap), mrao(mrao),
	baseTexture2(baseTexture2), normalMap2(normalMap2), mrao2(mrao2),
	blendTexture(blendTexture)
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

	blendFactor = uvw.z * triData.alphas[0] + uvw.x * triData.alphas[1] + uvw.y * triData.alphas[2];

	entIdx = ent.id;
	rawEnt = ent.rawEntity;
	submatIdx = triData.submatIdx;

	albedo = ent.colour;
	alpha = ent.colour.a;
}

void TraceResult::CalcBlendFactor()
{
	if (blendFactorSet) return;

	if (maskedBlending) blendFactor = 0.5f;
	if (blendTexture != nullptr) {
		CalcTexCoord();
		VTFPixel pixelBlend = blendTexture->GetPixel(
			floor(texUV.x * blendTexture->GetWidth()),
			floor(texUV.y * blendTexture->GetHeight()),
			0
		);

		if (maskedBlending) {
			blendFactor = pixelBlend.g;
		} else {
			float minb = glm::saturate(pixelBlend.g - pixelBlend.r);
			float maxb = glm::saturate(pixelBlend.g + pixelBlend.r);
			blendFactor = glm::smoothstep(minb, maxb, blendFactor);
		}
	}

	blendFactorSet = true;
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
		CalcTexCoord();
		CalcBlendFactor();

		VTFPixel pixelNormal = normalMap->GetPixel(
			floor(texUV.x * normalMap->GetWidth()),
			floor(texUV.y * normalMap->GetHeight()),
			0
		);
		glm::vec3 mappedNormal = glm::vec3(pixelNormal.r, pixelNormal.g, pixelNormal.b) * 2.f - 1.f;

		if (normalMap2 != nullptr) {
			pixelNormal = normalMap2->GetPixel(
				floor(texUV.x * normalMap2->GetWidth()),
				floor(texUV.y * normalMap2->GetHeight()),
				0
			);
			glm::vec3 mappedNormal2 = glm::vec3(pixelNormal.r, pixelNormal.g, pixelNormal.b) * 2.f - 1.f;

			mappedNormal = glm::normalize(glm::lerp(mappedNormal, mappedNormal2, blendFactor));
		}

		normal = glm::mat3{
			tangent[0],  tangent[1],  tangent[2],
			binormal[0], binormal[1], binormal[2],
			normal[0],   normal[1],   normal[2]
		} * mappedNormal;
		normal = glm::normalize(normal);

		tangent = glm::normalize(tangent - normal * glm::dot(tangent, normal));
		binormal = glm::cross(tangent, normal);
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
	CalcBlendFactor();

	VTFPixel pixelColour = baseTexture->GetPixel(
		floor(texUV.x * baseTexture->GetWidth()),
		floor(texUV.y * baseTexture->GetHeight()),
		0
	);
	glm::vec4 colour(pixelColour.r, pixelColour.g, pixelColour.b, pixelColour.a);

	if (baseTexture2 != nullptr) {
		pixelColour = baseTexture2->GetPixel(
			floor(texUV.x * baseTexture2->GetWidth()),
			floor(texUV.y * baseTexture2->GetHeight()),
			0
		);
		glm::vec4 colour2(pixelColour.r, pixelColour.g, pixelColour.b, pixelColour.a);

		colour = glm::lerp(colour, colour2, blendFactor);
	}

	albedo *= glm::vec3(colour.r, colour.g, colour.b);
	alpha *= colour.a;

	if (mrao != nullptr) {
		VTFPixel pixelMRAO = mrao->GetPixel(
			floor(texUV.x * mrao->GetWidth()),
			floor(texUV.y * mrao->GetHeight()),
			0
		);
		glm::vec2 metalnessRoughness(pixelMRAO.r, pixelMRAO.g);

		if (mrao2 != nullptr) {
			pixelMRAO = mrao2->GetPixel(
				floor(texUV.x * mrao2->GetWidth()),
				floor(texUV.y * mrao2->GetHeight()),
				0
			);
			glm::vec2 metalnessRoughness2(pixelMRAO.r, pixelMRAO.g);

			metalnessRoughness = glm::lerp(metalnessRoughness, metalnessRoughness2, blendFactor);
		}

		metalness = metalnessRoughness.r;
		roughness = metalnessRoughness.g;
	}

	shadingDataSet = true;
}

const glm::vec3& TraceResult::GetPos()
{
	if (!posSet) {
		pos = uvw.z * v[0] + uvw.x * v[1] + uvw.y * v[2];
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

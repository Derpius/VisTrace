#include "TraceResult.h"
#include "Utils.h"

#include "glm/gtx/compatibility.hpp"
using namespace glm;

using namespace VisTrace;

int TraceResult::id = -1;

vec4 TextureCombine(
	vec4 baseColour, vec4 detailColour,
	DetailBlendMode blendMode, float blendFactor
)
{
	switch (blendMode) {
	case DetailBlendMode::DecalModulate: {
		return baseColour * vec4(lerp(vec3(1.f), 2.f * vec3(detailColour), blendFactor), 1.f);
	} case DetailBlendMode::UnlitAdditive: // Given the lighting is entirely user implemented, these just fall through to standard additive
	case DetailBlendMode::UnlitAdditiveThresholdFade:
	case DetailBlendMode::Additive: {
		return baseColour + vec4(blendFactor * vec3(detailColour), 0.f);
	} case DetailBlendMode::TranslucentDetail: {
		float blend = blendFactor * detailColour.a;
		return vec4(lerp(vec3(baseColour), vec3(detailColour), blend), baseColour.a);
	} case DetailBlendMode::BlendFactorFade: {
		return lerp(baseColour, detailColour, blendFactor);
	} case DetailBlendMode::TranslucentBase: {
		float blend = blendFactor * (1.f - baseColour.a);
		return vec4(lerp(vec3(baseColour), vec3(detailColour), blend), detailColour.a);
	} case DetailBlendMode::TwoPatternDecalModulate: {
		float dc = lerp(detailColour.r, detailColour.a, baseColour.a);
		return baseColour * vec4(vec3(lerp(1.f, 2.f * dc, blendFactor)), 1.f);
	} case DetailBlendMode::Multiply: {
		return lerp(baseColour, baseColour * detailColour, blendFactor);
	} case DetailBlendMode::BaseMaskDetailAlpha: {
		return vec4(vec3(baseColour), lerp(baseColour.a, baseColour.a * detailColour.a, blendFactor));
	} case DetailBlendMode::SSBump: // Self shadowing bump maps not implemented (and dont seem to be widely used anyway)
	case DetailBlendMode::SSBumpAlbedo:
	default:
		return baseColour;
	}
}

TraceResult::TraceResult(
	const vec3& direction, float distance,
	float coneWidth, float coneAngle,
	const Triangle& tri,
	const vec2& uv,
	const Entity& ent, const Material mat
) :
	distance(distance),
	coneWidth(coneWidth), coneAngle(coneAngle), lodOffset(tri.lod), mipOverride(coneWidth < 0.f || coneAngle <= 0.f),
	material(mat)
{
	if (tri.ignoreNormalMap) {
		material.normalMap = nullptr;
		material.normalMap2 = nullptr;
	}

	wo = -direction;

	for (int i = 0; i < 3; i++) {
		vN[i] = tri.normals[i];
		vT[i] = tri.tangents[i];
		vB[i] = cross(vT[i], vN[i]);
		vUV[i] = tri.uvs[i];
	}

	v[0] = vec3(tri.p0[0], tri.p0[1], tri.p0[2]);
	Vector3 p1 = tri.p1(), p2 = tri.p2();
	v[1] = vec3(p1[0], p1[1], p1[2]);
	v[2] = vec3(p2[0], p2[1], p2[2]);

	uvw = vec3(uv, 1.f - uv[0] - uv[1]);
	geometricNormal = vec3(tri.nNorm[0], tri.nNorm[1], tri.nNorm[2]);

	blendFactor = uvw.z * tri.alphas[0] + uvw.x * tri.alphas[1] + uvw.y * tri.alphas[2];
	texUV = uvw[2] * vUV[0] + uvw[0] * vUV[1] + uvw[1] * vUV[2];

	entIdx = ent.id;
	rawEnt = ent.rawEntity;
	submatIdx = tri.material;

	albedo = ent.colour * material.colour;
	alpha = ent.colour.a * material.colour.a;

	hitSky = (material.surfFlags & BSPEnums::SURF::SKY) != BSPEnums::SURF::NONE;

	frontFacing = dot(wo, geometricNormal) >= 0.f;
}

// Ray Tracing Gems
void TraceResult::CalcFootprint()
{
	if (textureLodSet || mipOverride) return;
	using namespace glm;

	// Propagate cone given input parameters from starting width and angle to hit point
	coneWidth = coneAngle * distance + coneWidth;

	float normalTerm = dot(wo, geometricNormal);

	textureLodInfo = vec2(
		lodOffset,
		(coneWidth * coneWidth) / (normalTerm * normalTerm)
	);
	textureLodSet = true;
}

void TraceResult::CalcBlendFactor()
{
	if (blendFactorSet) return;

	if (material.maskedBlending) blendFactor = 0.5f;
	if (material.blendTexture != nullptr) {
		CalcFootprint();

		vec2 scaled = TransformTexcoord(texUV, material.blendTexMat, material.texScale);
		Pixel pixelBlend = material.blendTexture->Sample(
			scaled.x, scaled.y,
			mipOverride ? 0 : TriUVInfoToTexLOD(material.blendTexture, textureLodInfo)
		);

		if (material.maskedBlending) {
			blendFactor = pixelBlend.g;
		} else {
			float minb = saturate(pixelBlend.g - pixelBlend.r);
			float maxb = saturate(pixelBlend.g + pixelBlend.r);
			blendFactor = smoothstep(minb, maxb, blendFactor);
		}
	}

	blendFactorSet = true;
}

void TraceResult::CalcTBN()
{
	if (tbnSet) return;

	normal = normalize(uvw[2] * vN[0] + uvw[0] * vN[1] + uvw[1] * vN[2]);
	tangent = normalize(uvw[2] * vT[0] + uvw[0] * vT[1] + uvw[1] * vT[2]);
	binormal = normalize(uvw[2] * vB[0] + uvw[0] * vB[1] + uvw[1] * vB[2]);

	if (material.normalMap != nullptr) {
		CalcFootprint();
		CalcBlendFactor();

		vec2 scaled = TransformTexcoord(texUV, material.normalMapMat, material.texScale);
		Pixel pixelNormal = material.normalMap->Sample(
			scaled.x, scaled.y,
			mipOverride ? 0 : TriUVInfoToTexLOD(material.normalMap, textureLodInfo)
		);
		vec3 mappedNormal = vec3(pixelNormal.r, pixelNormal.g, pixelNormal.b) * 2.f - 1.f;

		if (material.normalMap2 != nullptr) {
			scaled = TransformTexcoord(texUV, material.normalMapMat2, material.texScale);
			pixelNormal = material.normalMap2->Sample(
				scaled.x, scaled.y,
				mipOverride ? 0 : TriUVInfoToTexLOD(material.normalMap2, textureLodInfo)
			);
			vec3 mappedNormal2 = vec3(pixelNormal.r, pixelNormal.g, pixelNormal.b) * 2.f - 1.f;

			mappedNormal = normalize(lerp(mappedNormal, mappedNormal2, blendFactor));
		}

		normal = mat3{
			tangent[0],  tangent[1],  tangent[2],
			binormal[0], binormal[1], binormal[2],
			normal[0],   normal[1],   normal[2]
		} * mappedNormal;
		normal = normalize(normal);

		tangent = normalize(tangent - normal * dot(tangent, normal));
		binormal = cross(tangent, normal);
	}

	const float kCosThetaThreshold = 0.1f;
	float cosTheta = abs(dot(wo, normal));
	if (cosTheta <= kCosThetaThreshold) {
		float t = saturate(cosTheta * (1.f / kCosThetaThreshold));
		normal = normalize(lerp(geometricNormal, normal, t));

		tangent = normalize(tangent - normal * dot(tangent, normal));
		binormal = cross(tangent, normal);
	}

	tbnSet = true;
}

void TraceResult::CalcShadingData()
{
	if (shadingDataSet) return;
	CalcFootprint();
	CalcBlendFactor();

	// Cache the scaled textures for both here cause we might use them again on the MRAO
	vec2 scaled = TransformTexcoord(texUV, material.baseTexMat, material.texScale);
	vec2 scaled2 = TransformTexcoord(texUV, material.baseTexMat2, material.texScale);

	Pixel pixelColour = material.baseTexture->Sample(
		scaled.x, scaled.y,
		mipOverride ? 0 : TriUVInfoToTexLOD(material.baseTexture, textureLodInfo)
	);
	vec4 colour(pixelColour.r, pixelColour.g, pixelColour.b, pixelColour.a);

	if (material.baseTexture2 != nullptr) {
		pixelColour = material.baseTexture2->Sample(
			scaled2.x, scaled2.y,
			mipOverride ? 0 : TriUVInfoToTexLOD(material.baseTexture2, textureLodInfo)
		);
		vec4 colour2(pixelColour.r, pixelColour.g, pixelColour.b, pixelColour.a);

		colour = lerp(colour, colour2, blendFactor);
	}

	if (material.detail != nullptr) {
		vec2 detailUVs = TransformTexcoord(texUV, material.detailMat, material.detailScale);
		Pixel detailColour = material.detail->Sample(
			detailUVs.x, detailUVs.y,
			mipOverride ? 0 : TriUVInfoToTexLOD(material.detail, textureLodInfo)
		);

		colour = clamp(TextureCombine(
			colour, vec4(detailColour.r, detailColour.g, detailColour.b, detailColour.a),
			material.detailBlendMode, material.detailBlendFactor
		), 0.f, 1.f);
	}

	albedo *= vec3(colour.r, colour.g, colour.b);
	alpha *= colour.a;

	if (material.mrao != nullptr) {
		Pixel pixelMRAO = material.mrao->Sample(
			scaled.x, scaled.y,
			mipOverride ? 0 : TriUVInfoToTexLOD(material.mrao, textureLodInfo)
		);
		vec2 metalnessRoughness(pixelMRAO.r, pixelMRAO.g);

		if (material.mrao2 != nullptr) {
			pixelMRAO = material.mrao2->Sample(
				scaled2.x, scaled2.y,
				mipOverride ? 0 : TriUVInfoToTexLOD(material.mrao2, textureLodInfo)
			);
			vec2 metalnessRoughness2(pixelMRAO.r, pixelMRAO.g);

			metalnessRoughness = lerp(metalnessRoughness, metalnessRoughness2, blendFactor);
		}

		metalness = metalnessRoughness.r;
		roughness = metalnessRoughness.g;
	}

	shadingDataSet = true;
}

const vec3& TraceResult::GetPos()
{
	if (!posSet) {
		pos = uvw.z * v[0] + uvw.x * v[1] + uvw.y * v[2];
		posSet = true;
	}
	return pos;
}

const vec3& TraceResult::GetNormal()
{
	CalcTBN();
	return normal;
}
const vec3& TraceResult::GetTangent()
{
	CalcTBN();
	return tangent;
}
const vec3& TraceResult::GetBinormal()
{
	CalcTBN();
	return binormal;
}

const vec3& TraceResult::GetAlbedo()
{
	CalcShadingData();
	return albedo;
}
float TraceResult::GetAlpha()
{
	CalcShadingData();
	return alpha;
}

float TraceResult::GetMetalness()
{
	CalcShadingData();
	return metalness;
}
float TraceResult::GetRoughness()
{
	CalcShadingData();
	return roughness;
}

float TraceResult::GetBaseMIPLevel()
{
	CalcFootprint();
	return mipOverride ? 0 : TriUVInfoToTexLOD(material.baseTexture, textureLodInfo);
}

std::string    TraceResult::GetMaterial()      const { return material.path; }
MaterialFlags  TraceResult::GetMaterialFlags() const { return material.flags; }
BSPEnums::SURF TraceResult::GetSurfFlags()     const { return material.surfFlags; }
bool           TraceResult::HitWater()         const { return material.water; }

std::string TraceResult::GetBaseTexture()   const { return material.baseTexture  != nullptr ? material.baseTexPath : ""; }
std::string TraceResult::GetNormalMap()     const { return material.normalMap    != nullptr ? material.normalMapPath : ""; }
std::string TraceResult::GetMRAO()          const { return material.mrao         != nullptr ? "vistrace/pbr/" + material.baseTexPath + "_mrao" : ""; }

std::string TraceResult::GetBaseTexture2()  const { return material.baseTexture2 != nullptr ? material.baseTexPath2 : ""; }
std::string TraceResult::GetNormalMap2()    const { return material.normalMap2   != nullptr ? material.normalMapPath2 : ""; }
std::string TraceResult::GetMRAO2()         const { return material.mrao2        != nullptr ? "vistrace/pbr/" + material.baseTexPath2 + "_mrao" : ""; }

std::string TraceResult::GetBlendTexture()  const { return material.blendTexture != nullptr ? material.blendTexPath : ""; }
std::string TraceResult::GetDetailTexture() const { return material.detail       != nullptr ? material.detailPath : ""; }

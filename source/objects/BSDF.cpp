#include "BSDF.h"

#include "yocto_shading.h"
using namespace yocto;

using namespace glm;

static const float kMinGGXAlpha = 0.0064f;

int BSDFMaterial::id = -1;

void BSDFMaterial::PrepShadingData(
	const vec3& hitColour, float hitMetalness, float hitRoughness,
	bool frontFacing
)
{
	if (!metallicOverridden) metallic = hitMetalness;
	if (!roughnessOverridden) roughness = hitRoughness;

	metallic = clamp(metallic, 0.f, 1.f);
	roughness = clamp(roughness, 0.f, 1.f);

	vec3 surfaceColour = clamp(baseColour * hitColour, 0.f, 1.f);
	dielectricReflection = surfaceColour; // TODO: allow the user to specify individual colours for each term
	reflectionTransmission = surfaceColour;
	conductive = surfaceColour;

	specularTransmission = clamp(specularTransmission, 0.f, 1.f);
	diffuseTransmission = clamp(diffuseTransmission, 0.f, 1.f);
}

// Calculates the probability of each lobe
// At the moment these are just the contribution of each lobe, which allows cancelling terms in the sampler
// If this ever changed then both the sample and eval functions would need changing too
inline void CalculateLobePDFs(
	const BSDFMaterial& data,
	float& pReflection, float& pReflectionTransmission,
	float& pConductive
)
{
	/*
		bsdf = (1 - metallic) * ((1 - specTrans) * reflection + specTrans * reflectionTransmission) + metallic * conductor
	*/
	pReflection             = (1.f - data.metallic) * (1.f - data.specularTransmission);
	pReflectionTransmission = (1.f - data.metallic) * data.specularTransmission;
	pConductive             = data.metallic;

	// This was done in falcor as the lobe selection probabilities may not have summed to 1, they should always do here
	/*float normFactor = pReflection + pReflectionTransmission + pConductive;
	if (normFactor > 0.f) {
		normFactor = 1.f / normFactor;

		pReflection             /= normFactor;
		pReflectionTransmission /= normFactor;
		pConductive             /= normFactor;
	}*/
}

#pragma region Reflective (Dielectric)
inline vec3f EvalGlossy(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& wo, const vec3f& wi
)
{
	// Lambert
	if (data.roughness == 1)
		return eval_matte(colour, normal, wo, wi);

	// Microfacet
	if (data.roughness >= kMinGGXAlpha)
		return eval_glossy(colour, data.ior, data.roughness, normal, wo, wi);

	// Delta
	vec3f upNormal = dot(normal, wo) <= 0 ? -normal : normal;
	float F = fresnel_dielectric(data.ior, upNormal, wo);
	return colour * (1.f - F) / pif * max(dot(upNormal, wi), 0.f) + vec3f{F, F, F} * (reflect(wo, upNormal) == wi ? 1.f : 0.f);
}

inline vec3f SampleGlossy(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& wo,
	Sampler* sg, LobeType& lobe
)
{
	vec2f r2{ 0, 0 };
	sg->GetFloat2D(r2.x, r2.y);

	// Lambert
	if (data.roughness == 1) {
		lobe = LobeType::DielectricReflection;
		return sample_matte(colour, normal, wo, r2);
	}

	// Microfacet
	if (data.roughness >= kMinGGXAlpha) {
		lobe = LobeType::DielectricReflection;
		return sample_glossy(colour, data.ior, data.roughness, normal, wo, sg->GetFloat(), r2);
	}

	// Delta
	vec3f upNormal = dot(normal, wo) <= 0 ? -normal : normal;
	if (sg->GetFloat() < fresnel_dielectric(data.ior, upNormal, wo)) {
		lobe = LobeType::DeltaDielectricReflection;
		return reflect(wo, upNormal);
	} else {
		lobe = LobeType::DielectricReflection;
		return sample_hemisphere_cos(upNormal, r2);
	}
}

inline float SampleGlossyPDF(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& wo, const vec3f& wi
)
{
	// Lambert
	if (data.roughness == 1)
		return sample_matte_pdf(colour, normal, wo, wi);

	// Microfacet
	if (data.roughness >= kMinGGXAlpha)
		return sample_glossy_pdf(colour, data.ior, data.roughness, normal, wo, wi);

	// Delta
	vec3f upNormal = dot(normal, wo) <= 0 ? -normal : normal;
	float F = fresnel_dielectric(data.ior, upNormal, wo);
	return F * (reflect(wo, upNormal) == wi ? 1.f : 0.f) + (1.f - F) * sample_hemisphere_cos_pdf(upNormal, wi);
}
#pragma endregion

#pragma region Reflective (Conductors)
inline vec3f EvalReflective(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& wo, const vec3f& wi
)
{
	return (data.roughness < kMinGGXAlpha) ? eval_reflective(colour, normal, wo, wi) : eval_reflective(colour, data.roughness, normal, wo, wi);
}

inline vec3f SampleReflective(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& wo,
	Sampler* sg, LobeType& lobe
)
{
	if (data.roughness >= kMinGGXAlpha) {
		vec2f r2{ 0, 0 };
		sg->GetFloat2D(r2.x, r2.y);

		lobe = LobeType::ConductiveReflection;
		return sample_reflective(colour, data.roughness, normal, wo, r2);
	}

	lobe = LobeType::DeltaConductiveReflection;
	return sample_reflective(colour, normal, wo);
}

inline float SampleReflectivePDF(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& wo, const vec3f& wi
)
{
	return (data.roughness < kMinGGXAlpha) ? sample_reflective_pdf(colour, normal, wo, wi) : sample_reflective_pdf(colour, data.roughness, normal, wo, wi);
}
#pragma endregion

#pragma region Refractive
inline vec3f EvalRefractive(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& wo, const vec3f& wi
)
{
	if (data.thin)
		return (data.roughness < kMinGGXAlpha) ? eval_transparent(colour, data.ior, normal, wo, wi) : eval_transparent(colour, data.ior, data.roughness, normal, wo, wi);
	else
		return (data.roughness < kMinGGXAlpha) ? eval_refractive(colour, data.ior, normal, wo, wi) : eval_refractive(colour, data.ior, data.roughness, normal, wo, wi);
}

inline vec3f SampleRefractive(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& wo,
	Sampler* sg, LobeType& lobe
)
{
	float r = sg->GetFloat();
	vec2f r2{ 0, 0 };
	if (data.roughness >= kMinGGXAlpha) sg->GetFloat2D(r2.x, r2.y);

	vec3f wi;
	bool delta = data.roughness < kMinGGXAlpha;

	if (data.thin)
		wi = delta ?
			sample_transparent(colour, data.ior, normal, wo, r) :
			sample_transparent(colour, data.ior, data.roughness, normal, wo, r, r2);
	else
		wi = delta ?
			sample_refractive(colour, data.ior, normal, wo, r) :
			sample_refractive(colour, data.ior, data.roughness, normal, wo, r, r2);

	const vec3f upNormal = dot(normal, wo) >= 0 ? normal : -normal;
	if (delta)
		lobe = same_hemisphere(upNormal, wo, wi) ? LobeType::DeltaDielectricReflection : LobeType::DeltaDielectricTransmission;
	else
		lobe = same_hemisphere(upNormal, wo, wi) ? LobeType::DielectricReflection : LobeType::DielectricTransmission;

	return wi;
}

inline float SampleRefractivePDF(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& wo, const vec3f& wi
)
{
	if (data.thin)
		return (data.roughness < kMinGGXAlpha) ?
			sample_tranparent_pdf(colour, data.ior, normal, wo, wi) :
			sample_tranparent_pdf(colour, data.ior, data.roughness, normal, wo, wi);
	else
		return (data.roughness < kMinGGXAlpha) ?
			sample_refractive_pdf(colour, data.ior, normal, wo, wi) :
			sample_refractive_pdf(colour, data.ior, data.roughness, normal, wo, wi);
}
#pragma endregion

bool SampleBSDF(
	const BSDFMaterial& data, Sampler* sg,
	const vec3& normal, const vec3& outgoing,
	BSDFSample& result
)
{
	vec3f dielectricReflection{ data.dielectricReflection.r, data.dielectricReflection.g, data.dielectricReflection.b };
	vec3f reflectionTransmission{ data.reflectionTransmission.r, data.reflectionTransmission.g, data.reflectionTransmission.b };
	vec3f conductive{ data.conductive.r, data.conductive.g, data.conductive.b };
	vec3f n{ normal.x, normal.y, normal.z };
	vec3f wo{ outgoing.x, outgoing.y, outgoing.z };

	float pReflection, pReflectionTransmission, pConductive;
	CalculateLobePDFs(data, pReflection, pReflectionTransmission, pConductive);

	float lobeSelect = sg->GetFloat();

	if (lobeSelect < pReflection) {
		vec2f r2;
		sg->GetFloat2D(r2.x, r2.y);

		vec3f wi = SampleGlossy(dielectricReflection, data, n, wo, sg, result.lobe);
		result.dir = vec3(wi.x, wi.y, wi.z);

		result.pdf = SampleGlossyPDF(dielectricReflection, data, n, wo, wi);
		if (result.pdf == 0) return false;

		vec3f weight = EvalGlossy(dielectricReflection, data, n, wo, wi) / result.pdf;
		result.weight = vec3(weight.x, weight.y, weight.z);

		result.pdf *= pReflection; // Apply this here as the prob is just the weighting of this lobe in the BSDF, so it cancels above
		if (pConductive > 0) result.pdf += pConductive * SampleReflectivePDF(conductive, data, n, wo, wi);
		if (pReflectionTransmission > 0) result.pdf += pReflectionTransmission * SampleRefractivePDF(reflectionTransmission, data, n, wo, wi);
	} else if (lobeSelect < pReflection + pReflectionTransmission) {
		vec3f wi = SampleRefractive(reflectionTransmission, data, n, wo, sg, result.lobe);
		result.dir = vec3(wi.x, wi.y, wi.z);

		result.pdf = SampleRefractivePDF(reflectionTransmission, data, n, wo, wi);
		if (result.pdf == 0) return false;

		vec3f weight = EvalRefractive(reflectionTransmission, data, n, wo, wi) / result.pdf;
		result.weight = vec3(weight.x, weight.y, weight.z);

		result.pdf *= pReflectionTransmission;
		if (pReflection > 0) result.pdf += pReflection * SampleGlossyPDF(dielectricReflection, data, n, wo, wi);
		if (pConductive > 0) result.pdf += pConductive * SampleReflectivePDF(conductive, data, n, wo, wi);
	} else if (pConductive > 0.f) {
		vec3f wi = SampleReflective(conductive, data, n, wo, sg, result.lobe);
		result.dir = vec3(wi.x, wi.y, wi.z);

		result.pdf = SampleReflectivePDF(conductive, data, n, wo, wi);
		if (result.pdf == 0) return false;

		vec3f weight = EvalReflective(conductive, data, n, wo, wi) / result.pdf;
		result.weight = vec3(weight.x, weight.y, weight.z);

		result.pdf *= pConductive;
		if (pReflection > 0) result.pdf += pReflection * SampleGlossyPDF(dielectricReflection, data, n, wo, wi);
		if (pReflectionTransmission > 0) result.pdf += pReflectionTransmission * SampleRefractivePDF(reflectionTransmission, data, n, wo, wi);
	}

	return true;
}

vec3 EvalBSDF(
	const BSDFMaterial& data,
	const vec3& normal, const vec3& outgoing, const vec3& incoming
)
{
	vec3f dielectricReflection{ data.dielectricReflection.r, data.dielectricReflection.g, data.dielectricReflection.b };
	vec3f reflectionTransmission{ data.reflectionTransmission.r, data.reflectionTransmission.g, data.reflectionTransmission.b };
	vec3f conductive{ data.conductive.r, data.conductive.g, data.conductive.b };
	vec3f n{ normal.x, normal.y, normal.z };
	vec3f wo{ outgoing.x, outgoing.y, outgoing.z };
	vec3f wi{ incoming.x, incoming.y, incoming.z };

	float pReflection, pReflectionTransmission, pConductive;
	CalculateLobePDFs(data, pReflection, pReflectionTransmission, pConductive);

	vec3f result{ 0, 0, 0 };
	if (pReflection > 0.f) {
		result += (1.f - data.metallic) * (1.f - data.specularTransmission) * EvalGlossy(dielectricReflection, data, n, wo, wi);
	}
	if (pReflectionTransmission > 0.f) {
		result += (1.f - data.metallic) * data.specularTransmission * EvalRefractive(reflectionTransmission, data, n, wo, wi);
	}
	if (pConductive > 0.f) {
		result += data.metallic * EvalReflective(conductive, data, n, wo, wi);
	}

	return vec3(result.x, result.y, result.z);
}

float EvalPDF(
	const BSDFMaterial& data,
	const vec3& normal, const vec3& outgoing, const vec3& incoming
)
{
	vec3f dielectricReflection{ data.dielectricReflection.r, data.dielectricReflection.g, data.dielectricReflection.b };
	vec3f reflectionTransmission{ data.reflectionTransmission.r, data.reflectionTransmission.g, data.reflectionTransmission.b };
	vec3f conductive{ data.conductive.r, data.conductive.g, data.conductive.b };
	vec3f n{ normal.x, normal.y, normal.z };
	vec3f wo{ outgoing.x, outgoing.y, outgoing.z };
	vec3f wi{ incoming.x, incoming.y, incoming.z };

	float pReflection, pReflectionTransmission, pConductive;
	CalculateLobePDFs(data, pReflection, pReflectionTransmission, pConductive);

	float pdf = 0.f;
	if (pReflection > 0.f) pdf += pReflection * SampleGlossyPDF(dielectricReflection, data, n, wo, wi);
	if (pReflectionTransmission > 0) pdf += pReflectionTransmission * SampleRefractivePDF(reflectionTransmission, data, n, wo, wi);
	if (pConductive > 0.f) pdf += pConductive * SampleReflectivePDF(conductive, data, n, wo, wi);

	return pdf;
}

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
	if (!metallicOverridden) metallic = clamp(hitMetalness, 0.f, 1.f);
	if (!roughnessOverridden) {
		roughness = clamp(hitRoughness, 0.f, 1.f);
		roughness *= roughness;
	}

	vec3 surfaceColour = clamp(baseColour * hitColour, 0.f, 1.f);
	dielectric = surfaceColour; // TODO: allow the user to specify individual colours for each BSDF
	conductor = surfaceColour;
}

// Calculates the probability of each lobe
// At the moment these are just the contribution of each lobe, which allows cancelling terms in the sampler
// If this ever changed then both the sample and eval functions would need changing too
inline void CalculateLobePDFs(
	const BSDFMaterial& data,
	float& pDielectric, float& pConductive
)
{
	// The dielectric BSDF calculates the distribution of each lobe
	pDielectric = (1.f - data.metallic);
	pConductive = data.metallic;
}

#pragma region Dielectric BSDF
inline vec3f EvalDielectric(
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

inline vec3f SampleDielectric(
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

inline float SampleDielectricPDF(
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

#pragma region Conductor BSDF
vec3f EvalConductor(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& incident, const vec3f& scattered
)
{
	const vec3f up_normal = dot(normal, incident) <= 0 ? -normal : normal;

	if (data.roughness < kMinGGXAlpha)
		return (reflect(incident, up_normal) == scattered ? fresnel_schlick(colour, up_normal, incident) : vec3f{ 0.f, 0.f, 0.f });

	const vec3f halfway = normalize(incident + scattered);

	const vec3f F = fresnel_schlick(colour, halfway, incident);
	const float D = microfacet_distribution(data.roughness, up_normal, halfway);
	const float G1incident = microfacet_shadowing1(data.roughness, up_normal, halfway, incident, true);
	const float G1scattered = microfacet_shadowing1(data.roughness, up_normal, halfway, scattered, true);

	float sDotN = dot(scattered, up_normal);

	vec3f bsdf = F * G1incident * G1scattered * D / (4 * dot(incident, up_normal) * sDotN) * yocto::abs(sDotN);
	bsdf *= microfacet_compensation(colour, data.roughness, normal, incident);

	return bsdf;
}

float SampleConductorPDF(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& incident, const vec3f& scattered
)
{
	const vec3f up_normal = dot(normal, incident) <= 0 ? -normal : normal;

	if (data.roughness < kMinGGXAlpha)
		return reflect(incident, up_normal) == scattered ? 1.f : 0.f;

	const vec3f halfway = normalize(incident + scattered);

	float iDotH = dot(incident, halfway);
	if (dot(up_normal, halfway) < 0.f) return 0.f;
	if (iDotH < 0.f) return 0.f;

	const float D = microfacet_distribution(data.roughness, up_normal, halfway);
	const float G1incident = microfacet_shadowing1(data.roughness, up_normal, halfway, incident, true);

	const float pdf = D * G1incident / (4 * dot(incident, up_normal));
	if (pdf <= 0.f || !std::isfinite(pdf)) return 0.f;
	return pdf;
}

bool SampleConductor(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& incident,
	Sampler* sg,
	LobeType& lobe, vec3f& scattered, vec3f& weight, float& pdf
)
{
	const vec3f up_normal = dot(normal, incident) <= 0 ? -normal : normal;

	if (data.roughness < kMinGGXAlpha) {
		lobe = LobeType::DeltaConductiveReflection;
		weight = fresnel_schlick(colour, up_normal, incident);
		pdf = 1;
		scattered = reflect(incident, up_normal);
		return true;
	}

	lobe = LobeType::ConductiveReflection;

	vec2f r2{ 0, 0 };
	sg->GetFloat2D(r2.x, r2.y);
	const vec3f halfway = sample_microfacet(data.roughness, up_normal, incident, r2);

	float iDotH = dot(incident, halfway);
	if (dot(up_normal, halfway) < 0.f) return false;
	if (iDotH < 0.f) return false;

	float iDotN = dot(incident, up_normal);

	scattered = reflect(incident, halfway);
	float sDotN = dot(scattered, up_normal);

	const vec3f F = fresnel_schlick(colour, halfway, incident);
	const float D = microfacet_distribution(data.roughness, up_normal, halfway);
	const float G1incident = microfacet_shadowing1(data.roughness, up_normal, halfway, incident, true);
	const float G1scattered = microfacet_shadowing1(data.roughness, up_normal, halfway, scattered, true);

	// eval: F * G1incident * G1scattered * D / (4 * iDotN * sDotN) * yocto::abs(sDotN)

	// pdf: D * G1incident / (4 * iDotN)

	// weight: eval / pdf
	// weight: eval * 1/pdf
	// 1/pdf: (4 * iDotN) / (D * G1incident)

	// weight:
	// F * G1incident * G1scattered * D / (4 * iDotN * sDotN) * (4 * iDotN) / (D * G1incident) * yocto::abs(sDotN)
	// cancel D
	// F * G1incident * G1scattered / (4 * iDotN * sDotN) * (4 * iDotN) / (G1incident) * yocto::abs(sDotN)
	// cancel G1incident
	// F * G1scattered / (4 * iDotN * sDotN) * (4 * iDotN) * yocto::abs(sDotN)
	// cancel 4
	// F * G1scattered / (iDotN * sDotN) * (iDotN) * yocto::abs(sDotN)
	// cancel iDotN
	// F * G1scattered / (sDotN) * yocto::abs(sDotN)
	// final weight (before compensation)
	// F * G1scattered / (sDotN) * yocto::abs(sDotN)

	pdf = D * G1incident / (4 * iDotN);
	if (pdf <= 0.f || !std::isfinite(pdf)) return false;

	weight = F * G1scattered / (sDotN) * yocto::abs(sDotN);
	weight *= microfacet_compensation(colour, data.roughness, normal, incident); // Add in single scattering compensation

	return true;
}

#pragma endregion

#pragma region Refractive
inline vec3f EvalRefractive(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& wo, const vec3f& wi
)
{
	if (data.roughness < kMinGGXAlpha) {
		bool entering = dot(normal, wo) >= 0;
		vec3f upNormal = entering ? normal : -normal;
		float invEta = entering ? (1.f / data.ior) : data.ior;

		vec3f deltaDir;
		if (dot(normal, wi) * dot(normal, wo) >= 0) {
			deltaDir = reflect(wo, upNormal);
		} else {
			deltaDir = data.thin ? -wo : refract(wo, upNormal, invEta);
		}

		if (wi != deltaDir) return vec3f{ 0.f, 0.f, 0.f };

		return data.thin ?
			eval_transparent(colour, data.ior, normal, wo, wi) :
			eval_refractive(colour, data.ior, normal, wo, wi);
	} else {
		return data.thin ?
			eval_transparent(colour, data.ior, data.roughness, normal, wo, wi) :
			eval_refractive(colour, data.ior, data.roughness, normal, wo, wi);
	}
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
	if (data.roughness < kMinGGXAlpha) {
		bool entering = dot(normal, wo) >= 0;
		vec3f upNormal = entering ? normal : -normal;
		float invEta = entering ? (1.f / data.ior) : data.ior;

		vec3f deltaDir;
		if (dot(normal, wi) * dot(normal, wo) >= 0) {
			deltaDir = reflect(wo, upNormal);
		} else {
			deltaDir = data.thin ? -wo : refract(wo, upNormal, invEta);
		}

		if (wi != deltaDir) return 0.f;

		return data.thin ?
			sample_tranparent_pdf(colour, data.ior, normal, wo, wi) :
			sample_refractive_pdf(colour, data.ior, normal, wo, wi);
	} else {
		return data.thin ?
			sample_tranparent_pdf(colour, data.ior, data.roughness, normal, wo, wi) :
			sample_refractive_pdf(colour, data.ior, data.roughness, normal, wo, wi);
	}
}
#pragma endregion

bool SampleBSDF(
	const BSDFMaterial& data, Sampler* sg,
	const vec3& normal, const vec3& outgoing,
	BSDFSample& result
)
{
	vec3f dielectric{ data.dielectric.r, data.dielectric.g, data.dielectric.b };
	vec3f conductor{ data.conductor.r, data.conductor.g, data.conductor.b };
	vec3f n{ normal.x, normal.y, normal.z };
	vec3f wo{ outgoing.x, outgoing.y, outgoing.z };

	float pDielectric, pConductor;
	CalculateLobePDFs(data, pDielectric, pConductor);

	float lobeSelect = sg->GetFloat();

	if (lobeSelect < pDielectric) {
		vec3f wi = SampleDielectric(dielectric, data, n, wo, sg, result.lobe);
		result.dir = vec3(wi.x, wi.y, wi.z);

		result.pdf = SampleDielectricPDF(dielectric, data, n, wo, wi);
		if (result.pdf == 0) return false;

		vec3f weight = EvalDielectric(dielectric, data, n, wo, wi) / result.pdf;
		result.weight = vec3(weight.x, weight.y, weight.z);

		result.pdf *= pDielectric; // Apply this here as the prob is just the weighting of this lobe in the BSDF, so it cancels above
		if (pConductor > 0) result.pdf += pConductor * SampleConductorPDF(conductor, data, n, wo, wi);
	} else if (pConductor > 0.f) {
		vec3f wi, weight;
		if (!SampleConductor(conductor, data, n, wo, sg, result.lobe, wi, weight, result.pdf)) return false;

		result.dir = vec3(wi.x, wi.y, wi.z);
		result.weight = vec3(weight.x, weight.y, weight.z);

		result.pdf *= pConductor;
		if (pDielectric > 0) result.pdf += pDielectric * SampleDielectricPDF(dielectric, data, n, wo, wi);
	}

	return true;
}

vec3 EvalBSDF(
	const BSDFMaterial& data,
	const vec3& normal, const vec3& outgoing, const vec3& incoming
)
{
	vec3f dielectric{ data.dielectric.r, data.dielectric.g, data.dielectric.b };
	vec3f conductor{ data.conductor.r, data.conductor.g, data.conductor.b };
	vec3f n{ normal.x, normal.y, normal.z };
	vec3f wo{ outgoing.x, outgoing.y, outgoing.z };
	vec3f wi{ incoming.x, incoming.y, incoming.z };

	float pDielectric, pConductor;
	CalculateLobePDFs(data, pDielectric, pConductor);

	vec3f result{ 0, 0, 0 };
	if (pDielectric > 0.f) {
		result += (1.f - data.metallic) * EvalDielectric(dielectric, data, n, wo, wi);
	}
	if (pConductor > 0.f) {
		result += data.metallic * EvalConductor(conductor, data, n, wo, wi);
	}

	return vec3(result.x, result.y, result.z);
}

float EvalPDF(
	const BSDFMaterial& data,
	const vec3& normal, const vec3& outgoing, const vec3& incoming
)
{
	vec3f dielectric{ data.dielectric.r, data.dielectric.g, data.dielectric.b };
	vec3f conductor{ data.conductor.r, data.conductor.g, data.conductor.b };
	vec3f n{ normal.x, normal.y, normal.z };
	vec3f wo{ outgoing.x, outgoing.y, outgoing.z };
	vec3f wi{ incoming.x, incoming.y, incoming.z };

	float pDielectric, pConductor;
	CalculateLobePDFs(data, pDielectric, pConductor);

	float pdf = 0.f;
	if (pDielectric > 0.f) pdf += pDielectric * SampleDielectricPDF(dielectric, data, n, wo, wi);
	if (pConductor > 0.f) pdf += pConductor * SampleConductorPDF(conductor, data, n, wo, wi);

	return pdf;
}

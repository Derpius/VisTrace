#include "BSDF.h"

#include "yocto_shading.h"
using namespace yocto;

using namespace glm;
using namespace VisTrace;

static const float kMinGGXAlpha = 0.0064f;

int BSDFMaterial::id = -1;

void BSDFMaterial::PrepShadingData(const vec3& hitColour, float hitMetalness, float hitRoughness)
{
	if (!metallicOverridden) metallic = clamp(hitMetalness, 0.f, 1.f);
	if (!roughnessOverridden) {
		linearRoughness = clamp(hitRoughness, 0.f, 1.f);
		roughness = linearRoughness * linearRoughness;
	}

	dielectric = clamp(dielectricInput * hitColour, 0.f, 1.f);
	conductor = clamp(conductorInput * hitColour, 0.f, 1.f);
}

inline void CalculateLobePDFs(
	const BSDFMaterial& data,
	float& pDiffuse, float& pSpecularReflection, float& pConductor, float& pSpecularReflectionTransmission
)
{
	bool diffuseActive = (data.activeLobes & LobeType::DiffuseReflection) != LobeType::None;

	bool specularReflectionActive, condutorActive, specularReflectionTransmissionActive;
	if (data.roughness < kMinGGXAlpha) {
		specularReflectionActive             = (data.activeLobes & LobeType::DeltaSpecularReflection)   != LobeType::None;
		condutorActive                       = (data.activeLobes & LobeType::DeltaConductiveReflection) != LobeType::None;
		specularReflectionTransmissionActive = (data.activeLobes & LobeType::DeltaSpecularTransmission) != LobeType::None;
	} else {
		specularReflectionActive             = (data.activeLobes & LobeType::SpecularReflection)        != LobeType::None;
		condutorActive                       = (data.activeLobes & LobeType::ConductiveReflection)      != LobeType::None;
		specularReflectionTransmissionActive = (data.activeLobes & LobeType::SpecularTransmission)      != LobeType::None;
	}
	specularReflectionTransmissionActive = specularReflectionTransmissionActive || specularReflectionActive;

	pDiffuse                        = diffuseActive                        ? (1.f - data.metallic) * (1.f - data.specularTransmission) : 0.f;
	pSpecularReflection             = specularReflectionActive             ? (1.f - data.metallic) * (1.f - data.specularTransmission) : 0.f;
	pConductor                      = condutorActive                       ? data.metallic                                             : 0.f;
	pSpecularReflectionTransmission = specularReflectionTransmissionActive ? (1.f - data.metallic) * data.specularTransmission         : 0.f;

	float normFactor = pDiffuse + pSpecularReflection + pConductor + pSpecularReflectionTransmission;
	if (normFactor > 0.f) {
		normFactor = 1.f / normFactor;

		pDiffuse                        *= normFactor;
		pSpecularReflection             *= normFactor;
		pConductor                      *= normFactor;
		pSpecularReflectionTransmission *= normFactor;
	}
}

// Appendix b https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_slides_v2.pdf
inline float microfacet_energy_fit(const float cosTheta, const float roughness)
{
	const float S[5] = { -0.170718f, 4.07985f, -11.5295f, 18.4961f, -9.23618f };
	const float T[5] = { 0.0632331f, 3.1434f, -7.47567f, 13.0482f, -7.0401f };

	const float r2 = roughness * roughness;
	const float r3 = r2 * roughness;
	const float r4 = r3 * roughness;

	const float s = S[0] * sqrtf(cosTheta) + S[1] * roughness + S[2] * r2 + S[3] * r3 + S[4] * r4;
	const float t = T[0] * cosTheta + T[1] * roughness + T[2] * r2 + T[3] * r3 + T[4] * r4;

	return 1 - powf(s, 6.f) * powf(cosTheta, 0.75f) / (powf(t, 6.f) + cosTheta * cosTheta);
}

// Appendix b https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_slides_v2.pdf
inline float microfacet_energy_avg_fit(const float roughness)
{
	const float A[3] = { 0.592665f, -1.47034f, 1.47196f };

	const float r2 = roughness * roughness;
	const float r3 = r2 * roughness;

	return A[0] * r3 / (1 + A[1] * roughness + A[2] * r2);
}

inline float spectrans_compensation(const float cosThetaR, const float cosThetaT, const float roughness, const float fresnel)
{
	const float EssR = microfacet_energy_fit(cosThetaR, roughness);
	const float EssT = microfacet_energy_fit(cosThetaT, roughness);
	return 1.f / (fresnel * EssR + (1 - fresnel) * EssT);
}

inline float schlick_dielectric(const float f0, const float cosTheta, const float f90 = 1.f)
{
	return f0 + (f90 - f0) * powf(1.f - cosTheta, 5.f);
}

// Slide 22 https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_slides_v2.pdf
inline vec3 schlicks_conductor_edgetint(
	const vec3& reflectance, const vec3& edgetint, const float falloff,
	const float cosTheta
)
{
	return reflectance + (edgetint - reflectance) * powf(1.f - cosTheta, 1.f / falloff);
}

// Slide 22 https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_slides_v2.pdf
inline vec3 schlicks_conductor_edgetint_avg(const vec3& reflectance, const vec3& edgetint, const float falloff)
{
	const float pSqr2 = 2.f * falloff * falloff;
	const float p3 = 3.f * falloff;
	return (edgetint * pSqr2 + reflectance + reflectance * p3) / (1 + p3 + pSqr2);
}

// Equations 8, 10, and 12  https://blog.selfshadow.com/publications/turquin/ms_comp_final.pdf
// Appendix a               https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_slides_v2.pdf
inline vec3 schlicks_conductor_compensation(
	const vec3& reflectance, const vec3& edgetint, const float falloff,
	const float linearRoughness,
	const float cosTheta
)
{
	const vec3 Fss  = schlicks_conductor_edgetint_avg(reflectance, edgetint, falloff);
	const float Ess  = microfacet_energy_fit(cosTheta, linearRoughness);
	const float Eavg = microfacet_energy_avg_fit(linearRoughness);

	const vec3 Fms  = Eavg / (1.f - Fss * (1.f - Eavg));

	return 1.f + Fms * (1.f - Ess) / Ess;
}

#pragma region Diffuse BRDF
vec3f EvalDiffuse(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& incident, const vec3f& scattered
)
{
	const vec3f upNormal = dot(normal, incident) <= 0 ? -normal : normal;
	const vec3f halfway = normalize(incident + scattered);

	const float iDotN = dot(incident, upNormal);
	const float sDotH = dot(scattered, halfway);
	const float sDotN = dot(scattered, upNormal);

	const float sDotHSat = clamp(sDotH, 0.f, 1.f);
	const float sDotNSat = clamp(sDotN, 0.f, 1.f);

	const float energyBias = lerp(0.f, 0.5f, data.linearRoughness);
	const float energyFactor = lerp(1.f, 1.f / 1.51f, data.linearRoughness);
	const float fd90 = energyBias + 2.f * sDotHSat * sDotHSat * data.linearRoughness;
	const float lightScatter = schlick_dielectric(1.f, sDotNSat, fd90);
	const float viewScatter = schlick_dielectric(1.f, iDotN, fd90);
	return colour * lightScatter * viewScatter * energyFactor / pif * sDotNSat;
}

float EvalDiffusePDF(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& incident, const vec3f& scattered
)
{
	const vec3f upNormal = dot(normal, incident) <= 0 ? -normal : normal;
	return sample_hemisphere_cos_pdf(upNormal, scattered);
}

bool SampleDiffuse(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& incident,
	ISampler* sg,
	LobeType& lobe, vec3f& scattered, vec3f& weight, float& pdf
)
{
	const vec3f upNormal = dot(normal, incident) <= 0 ? -normal : normal;

	lobe = LobeType::DiffuseReflection;

	vec2f r2{ 0, 0 };
	sg->GetFloat2D(r2.x, r2.y);

	scattered = sample_hemisphere_cos(upNormal, r2);
	pdf = sample_hemisphere_cos_pdf(upNormal, scattered);

	const vec3f halfway = normalize(incident + scattered);
	const float iDotN = dot(incident, upNormal);
	const float sDotH = dot(scattered, halfway);
	const float sDotN = dot(scattered, upNormal);

	const float sDotHSat = clamp(sDotH, 0.f, 1.f);
	const float sDotNSat = clamp(sDotN, 0.f, 1.f);

	const float energyBias = lerp(0.f, 0.5f, data.linearRoughness);
	const float energyFactor = lerp(1.f, 1.f / 1.51f, data.linearRoughness);
	const float fd90 = energyBias + 2.f * sDotHSat * sDotHSat * data.linearRoughness;
	const float lightScatter = schlick_dielectric(1.f, sDotNSat, fd90);
	const float viewScatter = schlick_dielectric(1.f, iDotN, fd90);

	weight = colour * lightScatter * viewScatter * energyFactor;
	return true;
}
#pragma endregion

#pragma region Specular BRDF
vec3f EvalSpecularReflection(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& incident, const vec3f& scattered
)
{
	const vec3f upNormal = dot(normal, incident) <= 0 ? -normal : normal;

	float F0 = (data.ior - 1.f) / (data.ior + 1.f);
	F0 *= F0;

	if (data.roughness < kMinGGXAlpha) {
		return reflect(incident, upNormal) == scattered ?
			vec3f{ 1.f, 1.f, 1.f } * schlick_dielectric(F0, dot(incident, upNormal)) :
			vec3f{ 0.f, 0.f, 0.f };
	}

	const vec3f halfway = normalize(incident + scattered);
	const float iDotN = dot(incident, upNormal);
	const float sDotH = dot(scattered, halfway);
	const float sDotN = dot(scattered, upNormal);

	const float F = schlick_dielectric(F0, dot(incident, halfway));
	const float D = microfacet_distribution(data.roughness, upNormal, halfway);
	const float G1incident = microfacet_shadowing1(data.roughness, upNormal, halfway, incident, true);
	const float G1scattered = microfacet_shadowing1(data.roughness, upNormal, halfway, scattered, true);

	vec3f specular = vec3f{ F, F, F } * G1incident * G1scattered * D / (4 * iDotN * sDotN) * yocto::abs(sDotN);
	specular *= microfacet_compensation(vec3f{ F0, F0, F0 }, data.roughness, halfway, scattered);
	return specular;
}

float EvalSpecularReflectionPDF(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& incident, const vec3f& scattered
)
{
	const vec3f upNormal = dot(normal, incident) <= 0 ? -normal : normal;

	if (data.roughness < kMinGGXAlpha) {
		return reflect(incident, upNormal) == scattered ? 1.f : 0.f;
	}

	const vec3f halfway = normalize(incident + scattered);

	float iDotH = dot(incident, halfway);
	if (dot(upNormal, halfway) < 0.f) return 0.f;
	if (iDotH < 0.f) return 0.f;

	const float D = microfacet_distribution(data.roughness, upNormal, halfway);
	const float G1incident = microfacet_shadowing1(data.roughness, upNormal, halfway, incident, true);

	const float pdf = D * G1incident * iDotH / (4 * dot(incident, upNormal) * dot(scattered, halfway));
	if (pdf < 0.f || !std::isfinite(pdf)) return 0.f;
	return pdf;
}

bool SampleSpecularReflection(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& incident,
	ISampler* sg,
	LobeType& lobe, vec3f& scattered, vec3f& weight, float& pdf
)
{
	const vec3f upNormal = dot(normal, incident) <= 0 ? -normal : normal;

	float F0 = (data.ior - 1.f) / (data.ior + 1.f);
	F0 *= F0;

	if (data.roughness < kMinGGXAlpha) {
		lobe = LobeType::DeltaSpecularReflection;
		scattered = reflect(incident, upNormal);
		weight = vec3f{ 1.f, 1.f, 1.f } * schlick_dielectric(F0, dot(incident, upNormal));
		pdf = 1.f;
		return true;
	}

	lobe = LobeType::SpecularReflection;

	vec2f r2{ 0, 0 };
	sg->GetFloat2D(r2.x, r2.y);
	const vec3f halfway = sample_microfacet(data.roughness, upNormal, incident, r2);

	float iDotH = dot(incident, halfway);
	if (dot(upNormal, halfway) < 0.f) return false;
	if (iDotH < 0.f) return false;

	float iDotN = dot(incident, upNormal);

	scattered = reflect(incident, halfway);
	float sDotN = dot(scattered, upNormal);
	float sDotH = dot(scattered, halfway);

	const float F = schlick_dielectric(F0, iDotH);
	const float D = microfacet_distribution(data.roughness, upNormal, halfway);
	const float G1incident = microfacet_shadowing1(data.roughness, upNormal, halfway, incident, true);
	const float G1scattered = microfacet_shadowing1(data.roughness, upNormal, halfway, scattered, true);

	pdf = D * G1incident * iDotH / (4 * iDotN * sDotH);
	if (pdf <= 0.f || !std::isfinite(pdf)) return false;

	weight = vec3f{ F, F, F } * G1scattered * sDotH / (sDotN * iDotH) * yocto::abs(sDotN);
	weight *= microfacet_compensation(vec3f{ F0, F0, F0 }, data.roughness, halfway, scattered);
	return true;
}
#pragma endregion

#pragma region Conductor BSDF
vec3 EvalConductor(
	const BSDFMaterial& data,
	const vec3f& normal, const vec3f& incident, const vec3f& scattered
)
{
	const vec3f upNormal = dot(normal, incident) <= 0 ? -normal : normal;

	if (data.roughness < kMinGGXAlpha)
		return (reflect(incident, upNormal) == scattered ?
			schlicks_conductor_edgetint(data.conductor, data.edgetint, data.falloff, dot(incident, upNormal)) :
			vec3(0.f, 0.f, 0.f));

	const vec3f halfway = normalize(incident + scattered);
	const float iDotH = dot(incident, halfway);
	const float sDotN = dot(scattered, upNormal);

	const vec3 F = schlicks_conductor_edgetint(data.conductor, data.edgetint, data.falloff, iDotH);
	const float D = microfacet_distribution(data.roughness, upNormal, halfway);
	const float G1incident = microfacet_shadowing1(data.roughness, upNormal, halfway, incident, true);
	const float G1scattered = microfacet_shadowing1(data.roughness, upNormal, halfway, scattered, true);

	vec3 bsdf = F * G1incident * G1scattered * D / (4 * dot(incident, upNormal) * sDotN) * yocto::abs(sDotN);
	bsdf *= schlicks_conductor_compensation(data.conductor, data.edgetint, data.falloff, data.linearRoughness, iDotH);

	return bsdf;
}

float EvalConductorPDF(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& incident, const vec3f& scattered
)
{
	const vec3f upNormal = dot(normal, incident) <= 0 ? -normal : normal;

	if (data.roughness < kMinGGXAlpha)
		return reflect(incident, upNormal) == scattered ? 1.f : 0.f;

	const vec3f halfway = normalize(incident + scattered);

	float iDotH = dot(incident, halfway);
	if (dot(upNormal, halfway) < 0.f) return 0.f;
	if (iDotH < 0.f) return 0.f;

	const float D = microfacet_distribution(data.roughness, upNormal, halfway);
	const float G1incident = microfacet_shadowing1(data.roughness, upNormal, halfway, incident, true);

	const float pdf = D * G1incident * iDotH / (4 * dot(incident, upNormal) * dot(scattered, halfway));
	if (pdf <= 0.f || !std::isfinite(pdf)) return 0.f;
	return pdf;
}

bool SampleConductor(
	const BSDFMaterial& data,
	const vec3f& normal, const vec3f& incident,
	ISampler* sg,
	LobeType& lobe, vec3f& scattered, vec3& weight, float& pdf
)
{
	const vec3f upNormal = dot(normal, incident) <= 0 ? -normal : normal;

	if (data.roughness < kMinGGXAlpha) {
		lobe = LobeType::DeltaConductiveReflection;
		weight = schlicks_conductor_edgetint(data.conductor, data.edgetint, data.falloff, dot(incident, upNormal));
		pdf = 1.f;
		scattered = reflect(incident, upNormal);
		return true;
	}

	lobe = LobeType::ConductiveReflection;

	vec2f r2{ 0, 0 };
	sg->GetFloat2D(r2.x, r2.y);
	const vec3f halfway = sample_microfacet(data.roughness, upNormal, incident, r2);

	float iDotH = dot(incident, halfway);
	if (dot(upNormal, halfway) < 0.f) return false;
	if (iDotH < 0.f) return false;

	float iDotN = dot(incident, upNormal);

	scattered = reflect(incident, halfway);
	float sDotN = dot(scattered, upNormal);
	float sDotH = dot(scattered, halfway);

	const vec3 F = schlicks_conductor_edgetint(data.conductor, data.edgetint, data.falloff, iDotH);
	const float D = microfacet_distribution(data.roughness, upNormal, halfway);
	const float G1incident = microfacet_shadowing1(data.roughness, upNormal, halfway, incident, true);
	const float G1scattered = microfacet_shadowing1(data.roughness, upNormal, halfway, scattered, true);

	// eval: F * G1incident * G1scattered * D / (4 * iDotN * sDotN) * yocto::abs(sDotN)

	// pdf: D * G1incident * iDotH / (4 * iDotN * sDotH)

	// weight: eval / pdf
	// weight: eval * 1/pdf
	// 1/pdf: (4 * iDotN * sDotH) / (D * G1incident * iDotH)

	// weight:
	// F * G1incident * G1scattered * D / (4 * iDotN * sDotN) * (4 * iDotN * sDotH) / (D * G1incident * iDotH) * yocto::abs(sDotN)
	// cancel D
	// F * G1incident * G1scattered / (4 * iDotN * sDotN) * (4 * iDotN * sDotH) / (G1incident * iDotH) * yocto::abs(sDotN)
	// cancel G1incident
	// F * G1scattered / (4 * iDotN * sDotN) * (4 * iDotN * sDotH) / (iDotH) * yocto::abs(sDotN)
	// cancel 4
	// F * G1scattered / (iDotN * sDotN) * (iDotN * sDotH) / (iDotH) * yocto::abs(sDotN)
	// cancel iDotN
	// F * G1scattered / (sDotN) * (sDotH) / (iDotH) * yocto::abs(sDotN)
	// final weight (before compensation)
	// F * G1scattered * sDotH / (sDotN * iDotH) * yocto::abs(sDotN)

	pdf = D * G1incident * iDotH / (4 * iDotN * sDotH);
	if (pdf <= 0.f || !std::isfinite(pdf)) return false;

	weight = F * G1scattered * sDotH / (sDotN * iDotH) * yocto::abs(sDotN);
	weight *= schlicks_conductor_compensation(data.conductor, data.edgetint, data.falloff, data.linearRoughness, iDotH);

	return true;
}

#pragma endregion

#pragma region Specular Transmission BSDF
vec3f EvalSpecularTransmission(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& incident, const vec3f& scattered
)
{
	const bool entering = dot(normal, incident) >= 0;
	const vec3f upNormal = entering ? normal : -normal;

	bool hasReflection = (data.activeLobes & (data.roughness < kMinGGXAlpha ? LobeType::DeltaSpecularReflection : LobeType::SpecularReflection)) != LobeType::None;
	bool hasTransmission = (data.activeLobes & (data.roughness < kMinGGXAlpha ? LobeType::DeltaSpecularTransmission : LobeType::SpecularTransmission)) != LobeType::None;

	const float iorI = entering ? 1.f : data.ior;
	const float iorS = entering ? data.ior : 1.f;
	const float eta = iorS / iorI;
	const float invEta = 1.f / eta;

	const bool isReflection = dot(normal, incident) * dot(normal, scattered) >= 0;
	if (isReflection && !hasReflection) return vec3f{ 0.f, 0.f, 0.f };
	if (!isReflection && !hasTransmission) return vec3f{ 0.f, 0.f, 0.f };

	if (data.roughness < kMinGGXAlpha) {
		if (data.thin) {
			if (isReflection) {
				if (scattered != reflect(incident, upNormal)) return vec3f{ 0.f, 0.f, 0.f };
				return vec3f{ 1.f, 1.f, 1.f } * fresnel_dielectric(data.ior, upNormal, incident);
			} else {
				if (scattered != -incident) return vec3f{ 0.f, 0.f, 0.f };
				return colour * (1.f - fresnel_dielectric(data.ior, upNormal, incident));
			}
		} else {
			if (isReflection) {
				if (scattered != reflect(incident, upNormal)) return vec3f{ 0.f, 0.f, 0.f };
				return vec3f{ 1.f, 1.f, 1.f } * fresnel_dielectric(eta, upNormal, incident);
			} else {
				if (scattered != refract(incident, upNormal, invEta)) return vec3f{ 0.f, 0.f, 0.f };
				return vec3f{ 1.f, 1.f, 1.f } * (1.f - fresnel_dielectric(eta, upNormal, incident));
			}
		}
	}

	if (data.thin) {
		return vec3f{ 0.f, 0.f, 0.f }; // Not implemented yet
	}

	const vec3f halfway = isReflection ? normalize(incident + scattered) : -normalize(iorI * incident + iorS * scattered);

	const float iDotH = dot(incident, halfway);
	const float iDotN = dot(incident, upNormal);
	const float sDotH = dot(scattered, halfway);
	const float sDotN = dot(scattered, upNormal);

	const float F = fresnel_dielectric(eta, halfway, incident);
	const float D = microfacet_distribution(data.roughness, upNormal, halfway);
	const float G1incident = microfacet_shadowing1(data.roughness, upNormal, halfway, incident, true);
	const float G1scattered = microfacet_shadowing1(data.roughness, upNormal, halfway, scattered, true);

	if (isReflection) {
		//spectrans_compensation(colour, data, halfway, F, scattered, refract(incident, halfway, invEta))
		const float compensation = spectrans_compensation(sDotH, sqrtf(1.f - invEta * invEta * (1.f - iDotH * iDotH)), data.linearRoughness, F);
		return vec3f{ F, F, F } * G1incident * G1scattered * D / (4 * iDotN * sDotN) * yocto::abs(sDotN) * compensation;
	}

	const float lhs = yocto::abs(iDotH) * yocto::abs(sDotH) / (yocto::abs(iDotN) * yocto::abs(sDotN));

	float denom = iorI * iDotH + iorS * sDotH;
	denom *= denom;
	const float rhs = iorS * iorS * (1.f - F) * G1incident * G1scattered * D / denom;

	const float compensation = spectrans_compensation(iDotH, sDotH, data.linearRoughness, F);
	return vec3f{ 1.f, 1.f, 1.f } * (lhs * rhs * yocto::abs(sDotN)) * compensation;
}

float EvalSpecularTransmissionPDF(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& incident, const vec3f& scattered
)
{
	const bool entering = dot(normal, incident) >= 0;
	const vec3f upNormal = entering ? normal : -normal;

	bool hasReflection = (data.activeLobes & (data.roughness < kMinGGXAlpha ? LobeType::DeltaSpecularReflection : LobeType::SpecularReflection)) != LobeType::None;
	bool hasTransmission = (data.activeLobes & (data.roughness < kMinGGXAlpha ? LobeType::DeltaSpecularTransmission : LobeType::SpecularTransmission)) != LobeType::None;

	const float iorI = entering ? 1.f : data.ior;
	const float iorS = entering ? data.ior : 1.f;
	const float eta = iorS / iorI;
	const float invEta = 1.f / eta;

	const bool isReflection = dot(normal, incident) * dot(normal, scattered) >= 0;
	if (isReflection && !hasReflection) return 0.f;
	if (!isReflection && !hasTransmission) return 0.f;

	if (data.roughness < kMinGGXAlpha) {
		if (isReflection) {
			if (scattered != reflect(incident, upNormal)) return 0.f;
			return fresnel_dielectric(data.thin ? data.ior : eta, upNormal, incident);
		} else {
			if (scattered != (data.thin ? -incident : refract(incident, upNormal, invEta))) return 0.f;
			return 1.f - fresnel_dielectric(data.thin ? data.ior : eta, upNormal, incident);
		}
	}

	if (data.thin) {
		return 0.f; // Not implemented yet
	}

	const vec3f halfway = isReflection ? normalize(incident + scattered) : -normalize(iorI * incident + iorS * scattered);

	const float iDotH = dot(incident, halfway);
	const float iDotN = dot(incident, upNormal);
	const float sDotH = dot(scattered, halfway);
	const float sDotN = dot(scattered, upNormal);

	if (dot(upNormal, halfway) < 0.f) return 0.f;
	if (iDotH < 0.f) return 0.f;

	const float F = fresnel_dielectric(eta, upNormal, incident);
	const float D = microfacet_distribution(data.roughness, upNormal, halfway);
	const float G1incident = microfacet_shadowing1(data.roughness, upNormal, halfway, incident, true);

	if (isReflection) {
		const float pdf = D * G1incident * iDotH / (4 * iDotN * sDotH);
		if (pdf < 0.f || !std::isfinite(pdf)) return 0.f;
		return F * pdf;
	}

	// pdf is VNDF pdf * jacobian of refraction
	// VNDF: G1incident * iDotH * D / iDotN
	// jacobian: iorS * iorS * abs(sDotH) / ((iorI * iDotH + iorS * sDotH) * (iorI * iDotH + iorS * sDotH))

	// final PDF:
	// iorS * iorS * abs(sDotH) / ((iorI * iDotH + iorS * sDotH) * (iorI * iDotH + iorS * sDotH)) * G1incident * iDotH * D / iDotN
	
	const float jDenom = iorI * iDotH + iorS * sDotH;
	const float jacobian = iorS * iorS * yocto::abs(sDotH) / (jDenom * jDenom);

	const float pdf = jacobian * G1incident * iDotH * D / iDotN;
	if (pdf < 0.f || !std::isfinite(pdf)) return 0.f;
	return (1.f - F) * pdf;
}

bool SampleSpecularTransmission(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& incident,
	ISampler* sg,
	LobeType& lobe, vec3f& scattered, vec3f& weight, float& pdf
)
{
	const bool entering = dot(normal, incident) >= 0;
	const vec3f upNormal = entering ? normal : -normal;

	const float iorI = entering ? 1.f : data.ior;
	const float iorS = entering ? data.ior : 1.f;
	const float eta = iorS / iorI;
	const float invEta = 1.f / eta;

	const float r = sg->GetFloat();

	if (data.roughness < kMinGGXAlpha) {
		if (data.thin) {
			const float F = fresnel_dielectric(data.ior, upNormal, incident);
			float pReflect = F;
			if ((data.activeLobes & LobeType::DeltaSpecularTransmission) == LobeType::None)
				pReflect = 1.f;
			else if ((data.activeLobes & LobeType::DeltaSpecularReflection) == LobeType::None)
				pReflect = 0.f;

			if (r < pReflect) {
				lobe = LobeType::DeltaSpecularReflection;
				weight = vec3f{ 1.f, 1.f, 1.f } * (F / pReflect);
				pdf = pReflect;
				scattered = reflect(incident, upNormal);
				return true;
			} else {
				lobe = LobeType::DeltaSpecularTransmission;
				weight = colour * ((1.f - F) / (1.f - pReflect));
				pdf = 1.f - pReflect;
				scattered = -incident;
				return true;
			}
		} else {
			const float F = fresnel_dielectric(eta, upNormal, incident);
			float pReflect = F;
			if ((data.activeLobes & LobeType::DeltaSpecularTransmission) == LobeType::None)
				pReflect = 1.f;
			else if ((data.activeLobes & LobeType::DeltaSpecularReflection) == LobeType::None)
				pReflect = 0.f;

			if (r < pReflect) {
				lobe = LobeType::DeltaSpecularReflection;
				weight = vec3f{ 1.f, 1.f, 1.f } * (F / pReflect);
				pdf = pReflect;
				scattered = reflect(incident, upNormal);
				return true;
			} else {
				lobe = LobeType::DeltaSpecularTransmission;
				weight = vec3f{ 1.f, 1.f, 1.f } * ((1.f - F) / (1.f - pReflect));
				pdf = 1.f - pReflect;
				scattered = refract(incident, upNormal, invEta);
				return true;
			}
		}
	}

	if (data.thin) {
		return false; // Not implemented yet
	}

	vec2f r2{ 0, 0 };
	sg->GetFloat2D(r2.x, r2.y);
	const vec3f halfway = sample_microfacet(data.roughness, upNormal, incident, r2);

	const float iDotH = dot(incident, halfway);
	const float iDotN = dot(incident, upNormal);
	if (dot(upNormal, halfway) < 0.f) return false;
	if (iDotH < 0.f) return false;

	const float F = fresnel_dielectric(eta, halfway, incident);
	const float D = microfacet_distribution(data.roughness, upNormal, halfway);
	const float G1incident = microfacet_shadowing1(data.roughness, upNormal, halfway, incident, true);

	float pReflect = F;
	if ((data.activeLobes & LobeType::SpecularTransmission) == LobeType::None)
		pReflect = 1.f;
	else if ((data.activeLobes & LobeType::SpecularReflection) == LobeType::None)
		pReflect = 0.f;

	if (r < pReflect) {
		lobe = LobeType::SpecularReflection;
		scattered = reflect(incident, halfway);
		const float sDotH = dot(scattered, halfway);
		const float sDotN = dot(scattered, upNormal);
		const float G1scattered = microfacet_shadowing1(data.roughness, upNormal, halfway, scattered, true);

		weight = vec3f{ F, F, F } * G1scattered * sDotH / (sDotN * iDotH) * yocto::abs(sDotN) / pReflect;
		weight *= spectrans_compensation(sDotH, sqrtf(1.f - invEta * invEta * (1.f - iDotH * iDotH)), data.linearRoughness, F);
		pdf = D * G1incident * iDotH / (4 * iDotN * sDotH) * pReflect;
		
		return true;
	} else {
		lobe = LobeType::SpecularTransmission;
		scattered = refract(incident, halfway, invEta);
		const float sDotH = dot(scattered, halfway);
		const float sDotN = dot(scattered, upNormal);
		const float G1scattered = microfacet_shadowing1(data.roughness, upNormal, halfway, scattered, true);

		float denom = iorI * iDotH + iorS * sDotH;
		denom *= denom;
		const float jacobian = iorS * iorS * yocto::abs(sDotH) / denom;

		// eval: yocto::abs(iDotH) * yocto::abs(sDotH) / (yocto::abs(iDotN) * yocto::abs(sDotN)) * iorS * iorS * (1.f - F) * G1incident * G1scattered * D / denom * yocto::abs(sDotN)

		// pdf: iorS * iorS * yocto::abs(sDotH) * G1incident * iDotH * D / (denom * iDotN)

		// weight: eval / pdf
		// weight: eval * 1/pdf
		// 1/pdf: (denom * iDotN) / (iorS * iorS * yocto::abs(sDotH) * G1incident * iDotH * D)

		// weight:
		// yocto::abs(iDotH) * yocto::abs(sDotH) / (yocto::abs(iDotN) * yocto::abs(sDotN)) * iorS * iorS * (1.f - F) * G1incident * G1scattered * D / denom * yocto::abs(sDotN)
		// * (denom * iDotN) / (iorS * iorS * yocto::abs(sDotH) * G1incident * iDotH * D)
		// 
		// Cancel denom
		// yocto::abs(iDotH) * yocto::abs(sDotH) / (yocto::abs(iDotN) * yocto::abs(sDotN)) * iorS * iorS * (1.f - F) * G1incident * G1scattered * D * yocto::abs(sDotN)
		// * (iDotN) / (iorS * iorS * yocto::abs(sDotH) * G1incident * iDotH * D)
		//
		// Cancel iorS^2
		// yocto::abs(iDotH) * yocto::abs(sDotH) / (yocto::abs(iDotN) * yocto::abs(sDotN)) * (1.f - F) * G1incident * G1scattered * D * yocto::abs(sDotN)
		// * (iDotN) / (yocto::abs(sDotH) * G1incident * iDotH * D)
		//
		// Cancel G1incident and D
		// yocto::abs(iDotH) * yocto::abs(sDotH) / (yocto::abs(iDotN) * yocto::abs(sDotN)) * (1.f - F) * G1scattered * yocto::abs(sDotN) * (iDotN) / (yocto::abs(sDotH) * iDotH)
		//
		// Cancel iDotN and iDotH (both are guaranteed >= 0 by the normal flipping and VNDF sampling)
		// yocto::abs(sDotH) / (yocto::abs(sDotN)) * (1.f - F) * G1scattered * yocto::abs(sDotN) / (yocto::abs(sDotH))
		// 
		// Cancel |sDotH| and |sDotN|
		// (1.f - F) * G1scattered

		weight = vec3f{ 1.f, 1.f, 1.f } * ((1.f - F) * G1scattered) / (1.f - pReflect);
		weight *= spectrans_compensation(iDotH, sDotH, data.linearRoughness, F);
		pdf = jacobian * G1incident * iDotH * D / iDotN * (1.f - pReflect);

		return true;
	}
}
#pragma endregion

bool SampleBSDF(
	const BSDFMaterial& data, ISampler* sg,
	const vec3& normal, const vec3& incident,
	BSDFSample& result
)
{
	vec3f dielectric{ data.dielectric.r, data.dielectric.g, data.dielectric.b };
	vec3f conductor{ data.conductor.r, data.conductor.g, data.conductor.b };
	vec3f n{ normal.x, normal.y, normal.z };
	vec3f wo{ incident.x, incident.y, incident.z };

	float pDiffuse, pSpecularReflection, pConductor, pSpecTrans;
	CalculateLobePDFs(data, pDiffuse, pSpecularReflection, pConductor, pSpecTrans);

	float lobeSelect = sg->GetFloat();

	if (lobeSelect < pDiffuse) {
		vec3f wi, weight;
		if (!SampleDiffuse(dielectric, data, n, wo, sg, result.lobe, wi, weight, result.pdf)) return false;

		result.scattered = vec3(wi.x, wi.y, wi.z);
		result.weight = vec3(weight.x, weight.y, weight.z) * (1.f - data.metallic) * (1.f - data.specularTransmission) / pDiffuse;

		result.pdf *= pDiffuse;
		if (pSpecularReflection > 0.f) result.pdf += pSpecularReflection * EvalSpecularReflectionPDF(dielectric, data, n, wo, wi);
		if (pSpecTrans > 0.f) result.pdf += pSpecTrans * EvalSpecularTransmissionPDF(dielectric, data, n, wo, wi);
		if (pConductor > 0.f) result.pdf += pConductor * EvalConductorPDF(conductor, data, n, wo, wi);
	} else if (lobeSelect < pDiffuse + pSpecularReflection) {
		vec3f wi, weight;
		if (!SampleSpecularReflection(dielectric, data, n, wo, sg, result.lobe, wi, weight, result.pdf)) return false;

		result.scattered = vec3(wi.x, wi.y, wi.z);
		result.weight = vec3(weight.x, weight.y, weight.z) * (1.f - data.metallic) * (1.f - data.specularTransmission) / pSpecularReflection;

		result.pdf *= pSpecularReflection;
		if (pDiffuse > 0.f) result.pdf += pDiffuse * EvalDiffusePDF(dielectric, data, n, wo, wi);
		if (pSpecTrans > 0.f) result.pdf += pSpecTrans * EvalSpecularTransmissionPDF(dielectric, data, n, wo, wi);
		if (pConductor > 0.f) result.pdf += pConductor * EvalConductorPDF(conductor, data, n, wo, wi);
	} else if (lobeSelect < pDiffuse + pSpecularReflection + pSpecTrans) {
		vec3f wi, weight;
		if (!SampleSpecularTransmission(dielectric, data, n, wo, sg, result.lobe, wi, weight, result.pdf)) return false;

		result.scattered = vec3(wi.x, wi.y, wi.z);
		result.weight = vec3(weight.x, weight.y, weight.z) * (1.f - data.metallic) * data.specularTransmission / pSpecTrans;

		result.pdf *= pSpecTrans;
		if (pDiffuse > 0.f) result.pdf += pDiffuse * EvalDiffusePDF(dielectric, data, n, wo, wi);
		if (pSpecularReflection > 0.f) result.pdf += pSpecularReflection * EvalSpecularReflectionPDF(dielectric, data, n, wo, wi);
		if (pConductor > 0.f) result.pdf += pConductor * EvalConductorPDF(conductor, data, n, wo, wi);
	} else if (pConductor > 0.f) {
		vec3f wi;
		if (!SampleConductor(data, n, wo, sg, result.lobe, wi, result.weight, result.pdf)) return false;

		result.scattered = vec3(wi.x, wi.y, wi.z);
		result.weight *= data.metallic / pConductor;

		result.pdf *= pConductor;
		if (pDiffuse > 0.f) result.pdf += pDiffuse * EvalDiffusePDF(dielectric, data, n, wo, wi);
		if (pSpecularReflection > 0.f) result.pdf += pSpecularReflection * EvalSpecularReflectionPDF(dielectric, data, n, wo, wi);
		if (pSpecTrans > 0.f) result.pdf += pSpecTrans * EvalSpecularTransmissionPDF(dielectric, data, n, wo, wi);
	}

	return true;
}

vec3 EvalBSDF(
	const BSDFMaterial& data,
	const vec3& normal, const vec3& incident, const vec3& scattered
)
{
	vec3f dielectric{ data.dielectric.r, data.dielectric.g, data.dielectric.b };
	vec3f conductor{ data.conductor.r, data.conductor.g, data.conductor.b };
	vec3f n{ normal.x, normal.y, normal.z };
	vec3f wo{ incident.x, incident.y, incident.z };
	vec3f wi{ scattered.x, scattered.y, scattered.z };

	float pDiffuse, pSpecularReflection, pConductor, pSpecTrans;
	CalculateLobePDFs(data, pDiffuse, pSpecularReflection, pConductor, pSpecTrans);

	vec3f result{ 0, 0, 0 };
	if (pDiffuse > 0.f)
		result += (1.f - data.metallic) * (1.f - data.specularTransmission) * EvalDiffuse(dielectric, data, n, wo, wi);
	if (pSpecularReflection > 0.f)
		result += (1.f - data.metallic) * (1.f - data.specularTransmission) * EvalSpecularReflection(dielectric, data, n, wo, wi);
	if (pConductor > 0.f) {
		vec3 tmp = data.metallic * EvalConductor(data, n, wo, wi);
		result += vec3f{ tmp.x, tmp.y, tmp.z };
	}
	if (pSpecTrans > 0.f)
		result += (1.f - data.metallic) * data.specularTransmission * EvalSpecularTransmission(dielectric, data, n, wo, wi);

	return vec3(result.x, result.y, result.z);
}

float EvalPDF(
	const BSDFMaterial& data,
	const vec3& normal, const vec3& incident, const vec3& scattered
)
{
	vec3f dielectric{ data.dielectric.r, data.dielectric.g, data.dielectric.b };
	vec3f conductor{ data.conductor.r, data.conductor.g, data.conductor.b };
	vec3f n{ normal.x, normal.y, normal.z };
	vec3f wo{ incident.x, incident.y, incident.z };
	vec3f wi{ scattered.x, scattered.y, scattered.z };

	float pDiffuse, pSpecularReflection, pConductor, pSpecTrans;
	CalculateLobePDFs(data, pDiffuse, pSpecularReflection, pConductor, pSpecTrans);

	float pdf = 0.f;
	if (pDiffuse > 0.f)
		pdf += pDiffuse * EvalDiffusePDF(dielectric, data, n, wo, wi);
	if (pSpecularReflection > 0.f)
		pdf += pSpecularReflection * EvalSpecularReflectionPDF(dielectric, data, n, wo, wi);
	if (pConductor > 0.f)
		pdf += pConductor * EvalConductorPDF(conductor, data, n, wo, wi);
	if (pSpecTrans > 0.f)
		pdf += pSpecTrans * EvalSpecularTransmissionPDF(dielectric, data, n, wo, wi);

	return pdf;
}

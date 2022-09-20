#include "BSDF.h"

#include "glm/ext/scalar_constants.hpp"
#include "glm/gtx/rotate_vector.hpp"

using namespace glm;
using namespace VisTrace;

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

// Slide 24 https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_slides_v2.pdf
// Note that the artist friendly roughness is in data.linearRoughness, this roughness is already squared
inline vec2 anisotropy_to_alpha(const float roughness, const float anisotropy)
{
	return vec2(
		roughness * (1 + anisotropy),
		roughness * (1 - anisotropy)
	);
}

#pragma region Hemisphere sampling
inline vec3 hemisphere_cos(ISampler* sg)
{
	const float r1 = sg->GetFloat();
	const float z = sqrtf(r1);
	const float sinTheta = sqrtf(1.f - r1);
	const float phi = 2.f * pi<float>() * sg->GetFloat();

	return vec3(sinTheta * cosf(phi), sinTheta * sinf(phi), z);
}

inline float hemisphere_cos_pdf(const float cosTheta)
{
	return (cosTheta > 0.f) ? (cosTheta / pi<float>()) : 0.f;
}
#pragma endregion

#pragma region Microfacets
inline float microfacet_d(const vec2& a, const vec3& n)
{
	const float c1 = n.x * n.x / (a.x * a.x) + n.y * n.y / (a.y * a.y) + n.z * n.z;
	return 1.f / (pi<float>() * a.x * a.y * c1 * c1);
}

inline float microfacet_g1(const vec2& a, const vec3& v)
{
	return 1.f / (1.f + (sqrtf(1.f + (a.x * a.x * v.x * v.x + a.y * a.y + v.y * v.y) / (v.z * v.z)) - 1.f) / 2.f);
}

// https://jcgt.org/published/0007/04/01/
inline vec3 microfacet_vndf(ISampler* sg, const vec2& a, const vec3& v)
{
	float U1, U2;
	sg->GetFloat2D(U1, U2);

	const vec3 Vh = normalize(vec3(a, 1.f) * v);

	const float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
	const vec3 T1 = lensq > 0.f ? vec3(-Vh.y, Vh.x, 0.f) * inversesqrt(lensq) : vec3(1.f, 0.f, 0.f);
	const vec3 T2 = cross(Vh, T1);

	const float r = sqrtf(U1);
	const float phi = 2.f * pi<float>() * U2;
	const float t1 = r * cosf(phi);
	float t2 = r * sinf(phi);
	const float s = 0.5f * (1.f + Vh.z);
	t2 = (1.f - s) * sqrtf(1.f - t1 * t1) + s * t2;

	const vec3 Nh = t1 * T1 + t2 * T2 + sqrtf(max(0.f, 1.f - t1 * t1 - t2 * t2)) * Vh;
	const vec3 Ne = normalize(vec3(a.x * Nh.x, a.y * Nh.y, max(0.f, Nh.z)));

	return Ne;
}

// Equation 3 https://jcgt.org/published/0007/04/01/
inline float microfacet_vndf_pdf(const vec2& a, const vec3& v, const vec3& n)
{
	return microfacet_g1(a, v) * max(0.f, dot(v, n)) * microfacet_d(a, n) / v.z;
}
#pragma endregion

#pragma region Energy and Fresnel
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

// Slide 18 https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_slides_v2.pdf
inline float fresnel_dielectric(const float eta, const float cosTheta)
{
	const float k = eta * eta + cosTheta * cosTheta - 1.f;
	if (k < 0.f) return 1.f;

	const float g = sqrtf(k);
	const float c1 = g + cosTheta;
	const float c2 = g - cosTheta;
	const float c3 = c2 / c1;
	const float c4 = (cosTheta * c1 - 1.f) / (cosTheta * c2 + 1.f);

	return 0.5 * c3 * c3 * (1.f + c4 * c4);
}

// Slide 18 https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_slides_v2.pdf
inline float fresnel_dielectric_avg(const float eta)
{
	return (eta < 1) ?
		(0.997118f + 0.1014f * eta - 0.965241f * eta * eta - 0.130607f * eta * eta * eta) :
		((eta - 1.f) / (4.08567 + 1.00071 * eta));
}

// Equations 8, 10, and 12  https://blog.selfshadow.com/publications/turquin/ms_comp_final.pdf
// Appendix a               https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_slides_v2.pdf
inline float microfacet_compensation(const float linearRoughness, const float Fss, const float cosTheta)
{
	const float Ess = microfacet_energy_fit(cosTheta, linearRoughness);
	const float Eavg = microfacet_energy_avg_fit(linearRoughness);

	const float Fms = Eavg / (1.f - Fss * (1.f - Eavg));

	return 1.f + Fms * (1.f - Ess) / Ess;
}
inline vec3 microfacet_compensation(const float linearRoughness, const vec3& Fss, const float cosTheta)
{
	const float Ess = microfacet_energy_fit(cosTheta, linearRoughness);
	const float Eavg = microfacet_energy_avg_fit(linearRoughness);

	const vec3 Fms = Eavg / (1.f - Fss * (1.f - Eavg));

	return 1.f + Fms * (1.f - Ess) / Ess;
}
#pragma endregion

#pragma region Diffuse BRDF
vec3 EvalDiffuse(
	const BSDFMaterial& data,
	const vec3& incident, const vec3& scattered
)
{
	if (scattered.z <= 0.f) return vec3(0.f, 0.f, 0.f);

	const vec3 halfway = normalize(incident + scattered);
	const float iDotN = incident.z;
	const float sDotH = dot(scattered, halfway);
	const float sDotN = scattered.z;

	const float energyBias = mix(0.f, 0.5f, data.linearRoughness);
	const float energyFactor = mix(1.f, 1.f / 1.51f, data.linearRoughness);
	const float fd90 = energyBias + 2.f * sDotH * sDotH * data.linearRoughness;
	const float lightScatter = schlick_dielectric(1.f, sDotN, fd90);
	const float viewScatter = schlick_dielectric(1.f, iDotN, fd90);
	return data.dielectric * lightScatter * viewScatter * energyFactor / pi<float>() * sDotN;
}

float EvalDiffusePDF(
	const BSDFMaterial& data, const vec3& scattered
)
{
	return hemisphere_cos_pdf(scattered.z);
}

bool SampleDiffuse(
	const BSDFMaterial& data,
	const vec3& incident,
	ISampler* sg,
	LobeType& lobe, vec3& scattered, vec3& weight, float& pdf
)
{
	lobe = LobeType::DiffuseReflection;

	scattered = hemisphere_cos(sg);
	pdf = hemisphere_cos_pdf(scattered.z);

	const vec3 halfway = normalize(incident + scattered);
	const float iDotN = incident.z;
	const float sDotH = dot(scattered, halfway);
	const float sDotN = scattered.z;

	const float energyBias = mix(0.f, 0.5f, data.linearRoughness);
	const float energyFactor = mix(1.f, 1.f / 1.51f, data.linearRoughness);
	const float fd90 = energyBias + 2.f * sDotH * sDotH * data.linearRoughness;
	const float lightScatter = schlick_dielectric(1.f, sDotN, fd90);
	const float viewScatter = schlick_dielectric(1.f, iDotN, fd90);

	weight = data.dielectric * lightScatter * viewScatter * energyFactor;
	return true;
}
#pragma endregion

#pragma region Specular BRDF
vec3 EvalSpecularReflection(const BSDFMaterial& data, const vec3& incident, const vec3& scattered)
{
	if (scattered.z < 0.f) return vec3(0.f, 0.f, 0.f);
	if (data.roughness < kMinGGXAlpha) return vec3(0.f, 0.f, 0.f);

	const float eta = data.ior; // TODO: user configurable incident IoR

	const vec3 halfway = normalize(incident + scattered);
	const float iDotN = incident.z;
	const float sDotH = dot(scattered, halfway);
	const float sDotN = scattered.z;

	const vec2 ggxAlpha = anisotropy_to_alpha(data.roughness, data.anisotropy);

	const float F = fresnel_dielectric(eta, dot(incident, halfway));
	const float D = microfacet_d(ggxAlpha, halfway);
	const float G1incident = microfacet_g1(ggxAlpha, incident);
	const float G1scattered = microfacet_g1(ggxAlpha, scattered);

	vec3 specular = vec3{ F, F, F } * G1incident * G1scattered * D / (4 * iDotN * sDotN) * sDotN;
	specular *= microfacet_compensation(data.linearRoughness, fresnel_dielectric_avg(eta), incident.z);
	return specular;
}

float EvalSpecularReflectionPDF(const BSDFMaterial& data, const vec3& incident, const vec3& scattered)
{
	if (data.roughness < kMinGGXAlpha) return 0.f;

	const vec3 halfway = normalize(incident + scattered);

	if (halfway.z < 0.f) return 0.f;
	float iDotH = dot(incident, halfway);
	if (iDotH < 0.f) return 0.f;

	const vec2 ggxAlpha = anisotropy_to_alpha(data.roughness, data.anisotropy);

	const float D = microfacet_d(ggxAlpha, halfway);
	const float G1incident = microfacet_g1(ggxAlpha, incident);

	const float pdf = D * G1incident * iDotH / (4 * incident.z * dot(scattered, halfway));
	if (pdf < 0.f || !std::isfinite(pdf)) return 0.f;
	return pdf;
}

bool SampleSpecularReflection(
	const BSDFMaterial& data,
	const vec3& incident,
	ISampler* sg,
	LobeType& lobe, vec3& scattered, vec3& weight, float& pdf
)
{
	const float eta = data.ior; // TODO: user configurable incident IoR

	if (data.roughness < kMinGGXAlpha) {
		lobe = LobeType::DeltaSpecularReflection;
		scattered = vec3(-incident.x, -incident.y, incident.z);
		weight = vec3( 1.f, 1.f, 1.f ) * fresnel_dielectric(eta, incident.z);
		pdf = 1.f;
		return true;
	}

	lobe = LobeType::SpecularReflection;

	const vec2 ggxAlpha = anisotropy_to_alpha(data.roughness, data.anisotropy);

	const vec3 halfway = microfacet_vndf(sg, ggxAlpha, incident);

	if (halfway.z < 0.f) return false;
	float iDotH = dot(incident, halfway);
	if (iDotH < 0.f) return false;

	float iDotN = incident.z;

	scattered = reflect(-incident, halfway);
	if (scattered.z < 0.f) return false;

	float sDotN = scattered.z;
	float sDotH = dot(scattered, halfway);

	const float F = fresnel_dielectric(eta, iDotH);
	const float D = microfacet_d(ggxAlpha, halfway);
	const float G1incident = microfacet_g1(ggxAlpha, incident);
	const float G1scattered = microfacet_g1(ggxAlpha, scattered);

	pdf = D * G1incident * iDotH / (4 * iDotN * sDotH);
	if (pdf <= 0.f || !std::isfinite(pdf)) return false;

	weight = vec3{ F, F, F } * G1scattered * sDotH / (sDotN * iDotH) * sDotN;
	weight *= microfacet_compensation(data.linearRoughness, fresnel_dielectric_avg(eta), incident.z);
	return true;
}
#pragma endregion

#pragma region Conductor BSDF
vec3 EvalConductor(
	const BSDFMaterial& data,
	const vec3& incident, const vec3& scattered
)
{
	if (scattered.z < 0.f) return vec3(0.f, 0.f, 0.f);
	if (data.roughness < kMinGGXAlpha) return vec3(0.f, 0.f, 0.f);

	const vec3 halfway = normalize(incident + scattered);
	const float iDotH = dot(incident, halfway);
	const float sDotN = scattered.z;

	const vec2 ggxAlpha = anisotropy_to_alpha(data.roughness, data.anisotropy);

	const vec3 F = schlicks_conductor_edgetint(data.conductor, data.edgetint, data.falloff, iDotH);
	const float D = microfacet_d(ggxAlpha, halfway);
	const float G1incident = microfacet_g1(ggxAlpha, incident);
	const float G1scattered = microfacet_g1(ggxAlpha, scattered);

	vec3 bsdf = F * G1incident * G1scattered * D / (4 * incident.z * sDotN) * sDotN;
	bsdf *= microfacet_compensation(
		data.linearRoughness,
		schlicks_conductor_edgetint_avg(data.conductor, data.edgetint, data.falloff),
		incident.z
	);

	return bsdf;
}

float EvalConductorPDF(
	const BSDFMaterial& data,
	const vec3& incident, const vec3& scattered
)
{
	if (scattered.z < 0.f) return 0.f;
	if (data.roughness < kMinGGXAlpha) return 0.f;

	const vec3 halfway = normalize(incident + scattered);

	if (halfway.z < 0.f) return 0.f;
	float iDotH = dot(incident, halfway);
	if (iDotH < 0.f) return 0.f;

	const vec2 ggxAlpha = anisotropy_to_alpha(data.roughness, data.anisotropy);

	const float D = microfacet_d(ggxAlpha, halfway);
	const float G1incident = microfacet_g1(ggxAlpha, incident);

	const float pdf = D * G1incident * iDotH / (4 * incident.z * dot(scattered, halfway));
	if (pdf <= 0.f || !std::isfinite(pdf)) return 0.f;
	return pdf;
}

bool SampleConductor(
	const BSDFMaterial& data,
	const vec3& incident,
	ISampler* sg,
	LobeType& lobe, vec3& scattered, vec3& weight, float& pdf
)
{
	if (data.roughness < kMinGGXAlpha) {
		lobe = LobeType::DeltaConductiveReflection;
		weight = schlicks_conductor_edgetint(data.conductor, data.edgetint, data.falloff, incident.z);
		pdf = 1.f;
		scattered = vec3(-incident.x, -incident.y, incident.z);
		return true;
	}

	lobe = LobeType::ConductiveReflection;

	const vec2 ggxAlpha = anisotropy_to_alpha(data.roughness, data.anisotropy);

	const vec3 halfway = microfacet_vndf(sg, ggxAlpha, incident);

	if (halfway.z < 0.f) return false;
	float iDotH = dot(incident, halfway);
	if (iDotH < 0.f) return false;

	float iDotN = incident.z;

	scattered = reflect(-incident, halfway);
	if (scattered.z < 0.f) return false;

	float sDotN = scattered.z;
	float sDotH = dot(scattered, halfway);

	const vec3 F = schlicks_conductor_edgetint(data.conductor, data.edgetint, data.falloff, iDotH);
	const float D = microfacet_d(ggxAlpha, halfway);
	const float G1incident = microfacet_g1(ggxAlpha, incident);
	const float G1scattered = microfacet_g1(ggxAlpha, scattered);

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

	weight = F * G1scattered * sDotH / (sDotN * iDotH) * sDotN;
	weight *= microfacet_compensation(
		data.linearRoughness,
		schlicks_conductor_edgetint_avg(data.conductor, data.edgetint, data.falloff),
		incident.z
	);

	return true;
}

#pragma endregion

#pragma region Specular Transmission BSDF
vec3 EvalSpecularTransmission(
	const BSDFMaterial& data,
	const vec3& incident, const vec3& scattered, const bool entering
)
{
	if (data.roughness < kMinGGXAlpha) return vec3(0.f, 0.f, 0.f);

	bool hasReflection = (data.activeLobes & (data.roughness < kMinGGXAlpha ? LobeType::DeltaSpecularReflection : LobeType::SpecularReflection)) != LobeType::None;
	bool hasTransmission = (data.activeLobes & (data.roughness < kMinGGXAlpha ? LobeType::DeltaSpecularTransmission : LobeType::SpecularTransmission)) != LobeType::None;

	const float iorI = entering ? 1.f : data.ior;
	const float iorS = entering ? data.ior : 1.f;
	const float eta = iorS / iorI;
	const float invEta = 1.f / eta;

	const bool isReflection = scattered.z >= 0.f;
	if (isReflection && !hasReflection) return vec3(0.f, 0.f, 0.f);
	if (!isReflection && !hasTransmission) return vec3(0.f, 0.f, 0.f);

	if (data.thin) {
		return vec3(0.f, 0.f, 0.f); // Not implemented yet
	}

	const vec3 halfway = isReflection ?
		normalize(incident + scattered) :
		((iorI < iorS ? -1.f : 1.f) * normalize(iorI * incident + iorS * scattered));
	if (halfway.z < 0.f) return vec3(0.f, 0.f, 0.f);

	const float iDotH = dot(incident, halfway);
	if (iDotH < 0.f) return vec3(0.f, 0.f, 0.f);

	const float iDotN = incident.z;
	const float sDotH = dot(scattered, halfway);
	const float sDotN = scattered.z;

	const vec2 ggxAlpha = anisotropy_to_alpha(data.roughness, data.anisotropy);

	const float F = fresnel_dielectric(eta, iDotH);
	const float D = microfacet_d(ggxAlpha, halfway);
	const float G1incident = microfacet_g1(ggxAlpha, incident);
	const float G1scattered = microfacet_g1(ggxAlpha, scattered);

	if (isReflection) {
		return vec3(F, F, F) * G1incident * G1scattered * D / (4 * iDotN * sDotN) * sDotN;
	}

	const float lhs = abs(iDotH) * abs(sDotH) / (abs(iDotN) * -sDotN);

	float denom = iorI * iDotH + iorS * sDotH;
	denom *= denom;
	const float rhs = iorS * iorS * (1.f - F) * G1incident * G1scattered * D / denom;

	return vec3(1.f, 1.f, 1.f) * (lhs * rhs * -sDotN);
}

float EvalSpecularTransmissionPDF(
	const BSDFMaterial& data,
	const vec3& incident, const vec3& scattered, const bool entering
)
{
	if (data.roughness < kMinGGXAlpha) return 0.f;

	bool hasReflection = (data.activeLobes & (data.roughness < kMinGGXAlpha ? LobeType::DeltaSpecularReflection : LobeType::SpecularReflection)) != LobeType::None;
	bool hasTransmission = (data.activeLobes & (data.roughness < kMinGGXAlpha ? LobeType::DeltaSpecularTransmission : LobeType::SpecularTransmission)) != LobeType::None;

	const float iorI = entering ? 1.f : data.ior;
	const float iorS = entering ? data.ior : 1.f;
	const float eta = iorS / iorI;
	const float invEta = 1.f / eta;

	const bool isReflection = scattered.z >= 0.f;
	if (isReflection && !hasReflection) return 0.f;
	if (!isReflection && !hasTransmission) return 0.f;

	if (data.thin) {
		return 0.f; // Not implemented yet
	}

	const vec3 halfway = isReflection ?
		normalize(incident + scattered) :
		((iorI < iorS ? -1.f : 1.f) * normalize(iorI * incident + iorS * scattered));
	if (halfway.z < 0.f) return 0.f;

	const float iDotH = dot(incident, halfway);
	if (iDotH < 0.f) return 0.f;

	const float iDotN = incident.z;
	const float sDotH = dot(scattered, halfway);
	const float sDotN = scattered.z;

	const vec2 ggxAlpha = anisotropy_to_alpha(data.roughness, data.anisotropy);

	const float F = fresnel_dielectric(eta, iDotH);
	float pReflect = F;
	if ((data.activeLobes & LobeType::SpecularTransmission) == LobeType::None)
		pReflect = 1.f;
	else if ((data.activeLobes & LobeType::SpecularReflection) == LobeType::None)
		pReflect = 0.f;

	const float D = microfacet_d(ggxAlpha, halfway);
	const float G1incident = microfacet_g1(ggxAlpha, incident);

	if (isReflection) {
		const float pdf = D * G1incident * iDotH / (4 * iDotN * sDotH);
		if (pdf < 0.f || !std::isfinite(pdf)) return 0.f;
		return pReflect * pdf;
	}

	// pdf is VNDF pdf * jacobian of refraction
	// VNDF: G1incident * iDotH * D / iDotN
	// jacobian: iorS * iorS * abs(sDotH) / ((iorI * iDotH + iorS * sDotH) * (iorI * iDotH + iorS * sDotH))

	// final PDF:
	// iorS * iorS * abs(sDotH) / ((iorI * iDotH + iorS * sDotH) * (iorI * iDotH + iorS * sDotH)) * G1incident * iDotH * D / iDotN
	
	const float jDenom = iorI * iDotH + iorS * sDotH;
	const float jacobian = iorS * iorS * abs(sDotH) / (jDenom * jDenom);

	const float pdf = jacobian * G1incident * iDotH * D / iDotN;
	if (pdf < 0.f || !std::isfinite(pdf)) return 0.f;
	return (1.f - pReflect) * pdf;
}

bool SampleSpecularTransmission(
	const BSDFMaterial& data,
	const vec3& incident, const bool entering,
	ISampler* sg,
	LobeType& lobe, vec3& scattered, vec3& weight, float& pdf
)
{
	const float iorI = entering ? 1.f : data.ior;
	const float iorS = entering ? data.ior : 1.f;
	const float eta = iorS / iorI;
	const float invEta = 1.f / eta;

	const float r = sg->GetFloat();

	if (data.roughness < kMinGGXAlpha) {
		if (data.thin) {
			const float F = fresnel_dielectric(data.ior, incident.z);
			float pReflect = F;
			if ((data.activeLobes & LobeType::DeltaSpecularTransmission) == LobeType::None)
				pReflect = 1.f;
			else if ((data.activeLobes & LobeType::DeltaSpecularReflection) == LobeType::None)
				pReflect = 0.f;

			if (r < pReflect) {
				lobe = LobeType::DeltaSpecularReflection;
				weight = vec3(1.f, 1.f, 1.f) * (F / pReflect);
				pdf = pReflect;
				scattered = vec3(-incident.x, -incident.y, incident.z);
				return true;
			} else {
				lobe = LobeType::DeltaSpecularTransmission;
				weight = data.dielectric * ((1.f - F) / (1.f - pReflect));
				pdf = 1.f - pReflect;
				scattered = -incident;
				return true;
			}
		} else {
			const float F = fresnel_dielectric(eta, incident.z);
			float pReflect = F;
			if ((data.activeLobes & LobeType::DeltaSpecularTransmission) == LobeType::None)
				pReflect = 1.f;
			else if ((data.activeLobes & LobeType::DeltaSpecularReflection) == LobeType::None)
				pReflect = 0.f;

			if (r < pReflect) {
				lobe = LobeType::DeltaSpecularReflection;
				weight = vec3(1.f, 1.f, 1.f) * (F / pReflect);
				pdf = pReflect;
				scattered = vec3(-incident.x, -incident.y, incident.z);
				return true;
			} else {
				lobe = LobeType::DeltaSpecularTransmission;
				weight = vec3(1.f, 1.f, 1.f) * ((1.f - F) / (1.f - pReflect));
				pdf = 1.f - pReflect;
				scattered = refract(-incident, vec3(0.f, 0.f, 1.f), invEta);
				return true;
			}
		}
	}

	if (data.thin) {
		return false; // Not implemented yet
	}

	const vec2 ggxAlpha = anisotropy_to_alpha(data.roughness, data.anisotropy);

	const vec3 halfway = microfacet_vndf(sg, ggxAlpha, incident);
	if (halfway.z < 0.f) return false;

	const float iDotH = dot(incident, halfway);
	if (iDotH < 0.f) return false;

	const float iDotN = incident.z;

	const float F = fresnel_dielectric(eta, iDotH);
	const float D = microfacet_d(ggxAlpha, halfway);
	const float G1incident = microfacet_g1(ggxAlpha, incident);

	float pReflect = F;
	if ((data.activeLobes & LobeType::SpecularTransmission) == LobeType::None)
		pReflect = 1.f;
	else if ((data.activeLobes & LobeType::SpecularReflection) == LobeType::None)
		pReflect = 0.f;

	if (r < pReflect) {
		lobe = LobeType::SpecularReflection;
		scattered = reflect(-incident, halfway);
		const float sDotH = dot(scattered, halfway);
		const float sDotN = scattered.z;
		const float G1scattered = microfacet_g1(ggxAlpha, scattered);

		weight = vec3(F, F, F) * G1scattered * sDotH / (sDotN * iDotH) * abs(sDotN) / pReflect;
		pdf = D * G1incident * iDotH / (4 * iDotN * sDotH) * pReflect;
		
		return true;
	} else {
		lobe = LobeType::SpecularTransmission;
		scattered = refract(-incident, halfway, invEta);
		const float sDotH = dot(scattered, halfway);
		const float sDotN = scattered.z;
		const float G1scattered = microfacet_g1(ggxAlpha, scattered);

		float denom = iorI * iDotH + iorS * sDotH;
		denom *= denom;
		const float jacobian = iorS * iorS * abs(sDotH) / denom;

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

		weight = vec3(1.f, 1.f, 1.f) * ((1.f - F) * G1scattered) / (1.f - pReflect);
		pdf = jacobian * G1incident * iDotH * D / iDotN * (1.f - pReflect);

		return true;
	}
}
#pragma endregion

inline vec3 to_local(const BSDFMaterial& data, const vec3& T, const vec3& B, const vec3& N, const vec3& v)
{
	return vec3(dot(v, rotate(T, data.anisotropicRotation, N)), dot(v, rotate(B, data.anisotropicRotation, N)), dot(v, N));
}
inline vec3 from_local(const BSDFMaterial& data, const vec3& T, const vec3& B, const vec3& N, const vec3& v)
{
	return rotate(T, data.anisotropicRotation, N) * v.x + rotate(B, data.anisotropicRotation, N) * v.y + N * v.z;
}

bool SampleBSDF(
	const BSDFMaterial& data, ISampler* sg,
	const vec3& normal, const vec3& tangent, const vec3& binormal,
	const vec3& incidentWorld,
	BSDFSample& result
)
{
	float pDiffuse, pSpecularReflection, pConductor, pSpecTrans;
	CalculateLobePDFs(data, pDiffuse, pSpecularReflection, pConductor, pSpecTrans);

	float lobeSelect = sg->GetFloat();

	const bool entering = dot(incidentWorld, normal) >= 0.f;
	const vec3 incident = entering ? to_local(data, tangent, binormal, normal, incidentWorld) : to_local(data, tangent, binormal, -normal, incidentWorld);

	if (lobeSelect < pDiffuse) {
		if (!SampleDiffuse(data, incident, sg, result.lobe, result.scattered, result.weight, result.pdf)) return false;

		result.weight *= (1.f - data.metallic) * (1.f - data.specularTransmission) / pDiffuse;

		result.pdf *= pDiffuse;
		if (pSpecularReflection > 0.f) result.pdf += pSpecularReflection * EvalSpecularReflectionPDF(data, incident, result.scattered);
		if (pSpecTrans > 0.f)          result.pdf += pSpecTrans          * EvalSpecularTransmissionPDF(data, incident, result.scattered, entering);
		if (pConductor > 0.f)          result.pdf += pConductor          * EvalConductorPDF(data, incident, result.scattered);
	} else if (lobeSelect < pDiffuse + pSpecularReflection) {
		if (!SampleSpecularReflection(data, incident, sg, result.lobe, result.scattered, result.weight, result.pdf)) return false;

		result.weight *= (1.f - data.metallic) * (1.f - data.specularTransmission) / pSpecularReflection;

		result.pdf *= pSpecularReflection;
		if (pDiffuse > 0.f)   result.pdf += pDiffuse   * EvalDiffusePDF(data, result.scattered);
		if (pSpecTrans > 0.f) result.pdf += pSpecTrans * EvalSpecularTransmissionPDF(data, incident, result.scattered, entering);
		if (pConductor > 0.f) result.pdf += pConductor * EvalConductorPDF(data, incident, result.scattered);
	} else if (lobeSelect < pDiffuse + pSpecularReflection + pSpecTrans) {
		if (!SampleSpecularTransmission(data, incident, entering, sg, result.lobe, result.scattered, result.weight, result.pdf)) return false;

		result.weight *= (1.f - data.metallic) * data.specularTransmission / pSpecTrans;

		result.pdf *= pSpecTrans;
		if (pDiffuse > 0.f)            result.pdf += pDiffuse            * EvalDiffusePDF(data, result.scattered);
		if (pSpecularReflection > 0.f) result.pdf += pSpecularReflection * EvalSpecularReflectionPDF(data, incident, result.scattered);
		if (pConductor > 0.f)          result.pdf += pConductor          * EvalConductorPDF(data, incident, result.scattered);
	} else if (pConductor > 0.f) {
		if (!SampleConductor(data, incident, sg, result.lobe, result.scattered, result.weight, result.pdf)) return false;

		result.weight *= data.metallic / pConductor;

		result.pdf *= pConductor;
		if (pDiffuse > 0.f) result.pdf += pDiffuse * EvalDiffusePDF(data, result.scattered);
		if (pSpecularReflection > 0.f) result.pdf += pSpecularReflection * EvalSpecularReflectionPDF(data, incident, result.scattered);
		if (pSpecTrans > 0.f) result.pdf += pSpecTrans * EvalSpecularTransmissionPDF(data, incident, result.scattered, entering);
	}

	result.scattered = entering ? from_local(data, tangent, binormal, normal, result.scattered) : from_local(data, tangent, binormal, -normal, result.scattered);
	return true;
}

vec3 EvalBSDF(
	const BSDFMaterial& data,
	const vec3& normal, const vec3& tangent, const vec3& binormal,
	const vec3& incidentWorld, const vec3& scatteredWorld
)
{
	float pDiffuse, pSpecularReflection, pConductor, pSpecTrans;
	CalculateLobePDFs(data, pDiffuse, pSpecularReflection, pConductor, pSpecTrans);

	const bool entering = dot(incidentWorld, normal) >= 0.f;
	const vec3 incident = entering ?
		to_local(data, tangent, binormal, normal, incidentWorld) :
		to_local(data, tangent, binormal, -normal, incidentWorld);
	const vec3 scattered = entering ?
		to_local(data, tangent, binormal, normal, scatteredWorld) :
		to_local(data, tangent, binormal, -normal, scatteredWorld);

	vec3 result{ 0, 0, 0 };
	if (pDiffuse > 0.f)
		result += (1.f - data.metallic) * (1.f - data.specularTransmission) * EvalDiffuse(data, incident, scattered);
	if (pSpecularReflection > 0.f)
		result += (1.f - data.metallic) * (1.f - data.specularTransmission) * EvalSpecularReflection(data, incident, scattered);
	if (pConductor > 0.f)
		result += data.metallic * EvalConductor(data, incident, scattered);
	if (pSpecTrans > 0.f)
		result += (1.f - data.metallic) * data.specularTransmission * EvalSpecularTransmission(data, incident, scattered, entering);

	return vec3(result.x, result.y, result.z);
}

float EvalPDF(
	const BSDFMaterial& data,
	const vec3& normal, const vec3& tangent, const vec3& binormal,
	const vec3& incidentWorld, const vec3& scatteredWorld
)
{
	float pDiffuse, pSpecularReflection, pConductor, pSpecTrans;
	CalculateLobePDFs(data, pDiffuse, pSpecularReflection, pConductor, pSpecTrans);

	const bool entering = dot(incidentWorld, normal) >= 0.f;
	const vec3 incident = entering ?
		to_local(data, tangent, binormal, normal, incidentWorld) :
		to_local(data, tangent, binormal, -normal, incidentWorld);
	const vec3 scattered = entering ?
		to_local(data, tangent, binormal, normal, scatteredWorld) :
		to_local(data, tangent, binormal, -normal, scatteredWorld);

	float pdf = 0.f;
	if (pDiffuse > 0.f)
		pdf += pDiffuse * EvalDiffusePDF(data, scattered);
	if (pSpecularReflection > 0.f)
		pdf += pSpecularReflection * EvalSpecularReflectionPDF(data, incident, scattered);
	if (pConductor > 0.f)
		pdf += pConductor * EvalConductorPDF(data, incident, scattered);
	if (pSpecTrans > 0.f)
		pdf += pSpecTrans * EvalSpecularTransmissionPDF(data, incident, scattered, entering);

	return pdf;
}

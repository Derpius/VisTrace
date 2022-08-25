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

	dielectric = clamp(dielectric * hitColour, 0.f, 1.f);
	conductor = clamp(conductor * hitColour, 0.f, 1.f);
}

inline void CalculateLobePDFs(
	const BSDFMaterial& data,
	float& pDielectric, float& pConductive, float& pSpecTrans
)
{
	bool conductiveActive, dielectricActive, specTransActive;
	if (data.roughness < kMinGGXAlpha) {
		conductiveActive = (data.activeLobes & LobeType::DeltaConductiveReflection) != LobeType::None;
		dielectricActive = (data.activeLobes & (LobeType::DeltaDielectricReflection | LobeType::DiffuseReflection)) != LobeType::None;
		specTransActive = (data.activeLobes & (LobeType::DeltaDielectricReflection | LobeType::DeltaDielectricTransmission)) != LobeType::None;
	} else {
		conductiveActive = (data.activeLobes & LobeType::ConductiveReflection) != LobeType::None;
		dielectricActive = (data.activeLobes & (LobeType::DielectricReflection | LobeType::DiffuseReflection)) != LobeType::None;
		specTransActive = (data.activeLobes & (LobeType::DielectricReflection | LobeType::DielectricTransmission)) != LobeType::None;
	}

	pDielectric = dielectricActive ? (1.f - data.metallic) * (1.f - data.specularTransmission) : 0.f;
	pSpecTrans = specTransActive ? (1.f - data.metallic) * data.specularTransmission : 0.f;
	pConductive = conductiveActive ? data.metallic : 0.f;

	float normFactor = pDielectric + pSpecTrans + pConductive;
	if (normFactor > 0.f) {
		normFactor = 1.f / normFactor;

		pDielectric *= normFactor;
		pSpecTrans *= normFactor;
		pConductive *= normFactor;
	}
}

#pragma region Dielectric BSDF
float schlick_disney(const float f0, const float f90, const float u)
{
	return f0 + (f90 - f0) * yocto::pow(1.f - u, 5.f);
}

vec3f EvalDielectric(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& incident, const vec3f& scattered
)
{
	const vec3f upNormal = dot(normal, incident) <= 0 ? -normal : normal;
	const float Fmacro = fresnel_dielectric(data.ior, upNormal, incident);

	bool hasSpecular = (data.activeLobes & (data.roughness < kMinGGXAlpha ? LobeType::DeltaDielectricReflection : LobeType::DielectricReflection)) != LobeType::None;
	bool hasDiffuse = (data.activeLobes & LobeType::DiffuseReflection) != LobeType::None;

	vec3f diffuse = vec3f{ 0, 0, 0 }, specular = vec3f{ 0, 0, 0 };
	const vec3f halfway = normalize(incident + scattered);

	const float iDotN = dot(incident, upNormal);
	const float sDotH = dot(scattered, halfway);
	const float sDotN = dot(scattered, upNormal);

	if (hasDiffuse) {
		const float sDotHSat = clamp(sDotH, 0.f, 1.f);
		const float sDotNSat = clamp(sDotN, 0.f, 1.f);

		const float energyBias = lerp(0.f, 0.5f, data.linearRoughness);
		const float energyFactor = lerp(1.f, 1.f / 1.51f, data.linearRoughness);
		const float fd90 = energyBias + 2.f * sDotHSat * sDotHSat * data.linearRoughness;
		const float lightScatter = schlick_disney(1.f, fd90, sDotNSat);
		const float viewScatter = schlick_disney(1.f, fd90, iDotN);
		diffuse = colour * lightScatter * viewScatter * energyFactor / pif * sDotNSat;
	}

	if (data.roughness < kMinGGXAlpha && hasSpecular) {
		specular = vec3f{ 1.f, 1.f, 1.f } * (reflect(incident, upNormal) == scattered ? 1.f : 0.f);
	} else if (hasSpecular) {
		const float F = fresnel_dielectric(data.ior, halfway, incident);
		const float D = microfacet_distribution(data.roughness, upNormal, halfway);
		const float G1incident = microfacet_shadowing1(data.roughness, upNormal, halfway, incident, true);
		const float G1scattered = microfacet_shadowing1(data.roughness, upNormal, halfway, scattered, true);

		specular = vec3f{ F, F, F } * G1incident * G1scattered * D / (4 * iDotN * sDotN) * yocto::abs(sDotN);
	}

	return (1.f - Fmacro) * diffuse + /* Fmacro * */ specular; // fresnel is already incorporated in the microfacet eval
}

float SampleDielectricPDF(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& incident, const vec3f& scattered
)
{
	const vec3f upNormal = dot(normal, incident) <= 0 ? -normal : normal;
	const float F = fresnel_dielectric(data.ior, upNormal, incident);

	float pSpecular = F;
	if ((data.activeLobes & LobeType::DiffuseReflection) == LobeType::None)
		pSpecular = 1.f;
	else if (
		(data.activeLobes & (data.roughness < kMinGGXAlpha ? LobeType::DeltaDielectricReflection : LobeType::DielectricReflection)) == LobeType::None
	) pSpecular = 0.f;

	const float diffusePdf = sample_hemisphere_cos_pdf(upNormal, scattered);
	float specularPdf;
	if (data.roughness < kMinGGXAlpha) {
		specularPdf = reflect(incident, upNormal) == scattered ? 1.f : 0.f;
	} else {
		const vec3f halfway = normalize(incident + scattered);

		float iDotH = dot(incident, halfway);
		if (dot(upNormal, halfway) < 0.f) return 0.f;
		if (iDotH < 0.f) return 0.f;

		const float D = microfacet_distribution(data.roughness, upNormal, halfway);
		const float G1incident = microfacet_shadowing1(data.roughness, upNormal, halfway, incident, true);

		specularPdf = D * G1incident * iDotH / (4 * dot(incident, upNormal) * dot(scattered, halfway));
		if (specularPdf < 0.f || !std::isfinite(specularPdf)) specularPdf = 0.f;
	}

	return pSpecular * specularPdf + (1.f - pSpecular) * diffusePdf;
}

bool SampleDielectric(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& incident,
	ISampler* sg,
	LobeType& lobe, vec3f& scattered, vec3f& weight, float& pdf
)
{
	const vec3f upNormal = dot(normal, incident) <= 0 ? -normal : normal;
	const float Fmacro = fresnel_dielectric(data.ior, upNormal, incident);

	float pSpecular = Fmacro;
	if ((data.activeLobes & LobeType::DiffuseReflection) == LobeType::None)
		pSpecular = 1.f;
	else if (
		(data.activeLobes & (data.roughness < kMinGGXAlpha ? LobeType::DeltaDielectricReflection : LobeType::DielectricReflection)) == LobeType::None
	) pSpecular = 0.f;
	// Note that there's no need to handle when both the diffuse and specular lobes are inactive, as this should be handled higher in the pipeline

	if (sg->GetFloat() < pSpecular) {
		if (data.roughness < kMinGGXAlpha) {
			lobe = LobeType::DeltaDielectricReflection;
			scattered = reflect(incident, upNormal);
			weight = vec3f{ 1.f, 1.f, 1.f };
			pdf = Fmacro;
			return true;
		}
		lobe = LobeType::DielectricReflection;

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

		const float F = fresnel_dielectric(data.ior, halfway, incident);
		const float D = microfacet_distribution(data.roughness, upNormal, halfway);
		const float G1incident = microfacet_shadowing1(data.roughness, upNormal, halfway, incident, true);
		const float G1scattered = microfacet_shadowing1(data.roughness, upNormal, halfway, scattered, true);

		pdf = pSpecular * D * G1incident * iDotH / (4 * iDotN * sDotH);
		if (pdf <= 0.f || !std::isfinite(pdf)) return false;

		weight = vec3f{ F, F, F } * G1scattered * sDotH / (sDotN * iDotH) * yocto::abs(sDotN) / pSpecular;
		return true;
	} else {
		lobe = LobeType::DiffuseReflection;

		vec2f r2{ 0, 0 };
		sg->GetFloat2D(r2.x, r2.y);

		scattered = sample_hemisphere_cos(upNormal, r2);
		pdf = (1.f - pSpecular) * sample_hemisphere_cos_pdf(upNormal, scattered);

		const vec3f halfway = normalize(incident + scattered);
		const float iDotN = dot(incident, upNormal);
		const float sDotH = dot(scattered, halfway);
		const float sDotN = dot(scattered, upNormal);

		const float sDotHSat = clamp(sDotH, 0.f, 1.f);
		const float sDotNSat = clamp(sDotN, 0.f, 1.f);

		const float energyBias = lerp(0.f, 0.5f, data.linearRoughness);
		const float energyFactor = lerp(1.f, 1.f / 1.51f, data.linearRoughness);
		const float fd90 = energyBias + 2.f * sDotHSat * sDotHSat * data.linearRoughness;
		const float lightScatter = schlick_disney(1.f, fd90, sDotNSat);
		const float viewScatter = schlick_disney(1.f, fd90, iDotN);
		weight = colour * lightScatter * viewScatter * energyFactor / (1.f - pSpecular);

		return true;
	}
}
#pragma endregion

#pragma region Conductor BSDF
vec3f EvalConductor(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& incident, const vec3f& scattered
)
{
	const vec3f upNormal = dot(normal, incident) <= 0 ? -normal : normal;

	if (data.roughness < kMinGGXAlpha)
		return (reflect(incident, upNormal) == scattered ? fresnel_schlick(colour, upNormal, incident) : vec3f{ 0.f, 0.f, 0.f });

	const vec3f halfway = normalize(incident + scattered);

	const vec3f F = fresnel_schlick(colour, halfway, incident);
	const float D = microfacet_distribution(data.roughness, upNormal, halfway);
	const float G1incident = microfacet_shadowing1(data.roughness, upNormal, halfway, incident, true);
	const float G1scattered = microfacet_shadowing1(data.roughness, upNormal, halfway, scattered, true);

	float sDotN = dot(scattered, upNormal);

	vec3f bsdf = F * G1incident * G1scattered * D / (4 * dot(incident, upNormal) * sDotN) * yocto::abs(sDotN);
	bsdf *= microfacet_compensation(colour, data.roughness, normal, incident);

	return bsdf;
}

float SampleConductorPDF(
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
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& incident,
	ISampler* sg,
	LobeType& lobe, vec3f& scattered, vec3f& weight, float& pdf
)
{
	const vec3f upNormal = dot(normal, incident) <= 0 ? -normal : normal;

	if (data.roughness < kMinGGXAlpha) {
		lobe = LobeType::DeltaConductiveReflection;
		weight = fresnel_schlick(colour, upNormal, incident);
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

	const vec3f F = fresnel_schlick(colour, halfway, incident);
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
	weight *= microfacet_compensation(colour, data.roughness, normal, incident); // Add in single scattering compensation

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

	bool hasReflection = (data.activeLobes & (data.roughness < kMinGGXAlpha ? LobeType::DeltaDielectricReflection : LobeType::DielectricReflection)) != LobeType::None;
	bool hasTransmission = (data.activeLobes & (data.roughness < kMinGGXAlpha ? LobeType::DeltaDielectricTransmission : LobeType::DielectricTransmission)) != LobeType::None;

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
		return vec3f{ F, F, F } * G1incident * G1scattered * D / (4 * iDotN * sDotN) * yocto::abs(sDotN);
	}

	const float lhs = yocto::abs(iDotH) * yocto::abs(sDotH) / (yocto::abs(iDotN) * yocto::abs(sDotN));

	float denom = iorI * iDotH + iorS * sDotH;
	denom *= denom;
	const float rhs = iorS * iorS * (1.f - F) * G1incident * G1scattered * D / denom;

	return vec3f{ 1.f, 1.f, 1.f } * (lhs * rhs * yocto::abs(sDotN));
}

float SampleSpecularTransmissionPDF(
	const vec3f& colour, const BSDFMaterial& data,
	const vec3f& normal, const vec3f& incident, const vec3f& scattered
)
{
	const bool entering = dot(normal, incident) >= 0;
	const vec3f upNormal = entering ? normal : -normal;

	bool hasReflection = (data.activeLobes & (data.roughness < kMinGGXAlpha ? LobeType::DeltaDielectricReflection : LobeType::DielectricReflection)) != LobeType::None;
	bool hasTransmission = (data.activeLobes & (data.roughness < kMinGGXAlpha ? LobeType::DeltaDielectricTransmission : LobeType::DielectricTransmission)) != LobeType::None;

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
			if ((data.activeLobes & LobeType::DeltaDielectricTransmission) == LobeType::None)
				pReflect = 1.f;
			else if ((data.activeLobes & LobeType::DeltaDielectricReflection) == LobeType::None)
				pReflect = 0.f;

			if (r < pReflect) {
				lobe = LobeType::DeltaDielectricReflection;
				weight = vec3f{ 1.f, 1.f, 1.f } * (F / pReflect);
				pdf = pReflect;
				scattered = reflect(incident, upNormal);
				return true;
			} else {
				lobe = LobeType::DeltaDielectricTransmission;
				weight = colour * ((1.f - F) / (1.f - pReflect));
				pdf = 1.f - pReflect;
				scattered = -incident;
				return true;
			}
		} else {
			const float F = fresnel_dielectric(eta, upNormal, incident);
			float pReflect = F;
			if ((data.activeLobes & LobeType::DeltaDielectricTransmission) == LobeType::None)
				pReflect = 1.f;
			else if ((data.activeLobes & LobeType::DeltaDielectricReflection) == LobeType::None)
				pReflect = 0.f;

			if (r < pReflect) {
				lobe = LobeType::DeltaDielectricReflection;
				weight = vec3f{ 1.f, 1.f, 1.f } * (F / pReflect);
				pdf = pReflect;
				scattered = reflect(incident, upNormal);
				return true;
			} else {
				lobe = LobeType::DeltaDielectricTransmission;
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
	if ((data.activeLobes & LobeType::DielectricTransmission) == LobeType::None)
		pReflect = 1.f;
	else if ((data.activeLobes & LobeType::DielectricReflection) == LobeType::None)
		pReflect = 0.f;

	if (r < pReflect) {
		lobe = LobeType::DielectricReflection;
		scattered = reflect(incident, halfway);
		const float sDotH = dot(scattered, halfway);
		const float sDotN = dot(scattered, upNormal);
		const float G1scattered = microfacet_shadowing1(data.roughness, upNormal, halfway, scattered, true);

		weight = vec3f{ F, F, F } * G1scattered * sDotH / (sDotN * iDotH) * yocto::abs(sDotN) / pReflect;
		pdf = D * G1incident * iDotH / (4 * iDotN * sDotH) * pReflect;
		
		return true;
	} else {
		lobe = LobeType::DielectricTransmission;
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
		pdf = jacobian * G1incident * iDotH * D / iDotN * (1.f - pReflect);

		return true;
	}
}
#pragma endregion

bool SampleBSDF(
	const BSDFMaterial& data, ISampler* sg,
	const vec3& normal, const vec3& outgoing,
	BSDFSample& result
)
{
	vec3f dielectric{ data.dielectric.r, data.dielectric.g, data.dielectric.b };
	vec3f conductor{ data.conductor.r, data.conductor.g, data.conductor.b };
	vec3f n{ normal.x, normal.y, normal.z };
	vec3f wo{ outgoing.x, outgoing.y, outgoing.z };

	float pDielectric, pConductor, pSpecTrans;
	CalculateLobePDFs(data, pDielectric, pConductor, pSpecTrans);

	float lobeSelect = sg->GetFloat();

	if (lobeSelect < pDielectric) {
		vec3f wi, weight;
		if (!SampleDielectric(dielectric, data, n, wo, sg, result.lobe, wi, weight, result.pdf)) return false;

		result.dir = vec3(wi.x, wi.y, wi.z);
		result.weight = vec3(weight.x, weight.y, weight.z) * (1.f - data.metallic) * (1.f - data.specularTransmission) / pDielectric;

		result.pdf *= pDielectric;
		if (pConductor > 0) result.pdf += pConductor * SampleConductorPDF(conductor, data, n, wo, wi);
		if (pSpecTrans > 0.f) result.pdf += pSpecTrans * SampleSpecularTransmissionPDF(dielectric, data, n, wo, wi);
	} else if (lobeSelect < pDielectric + pSpecTrans) {
		vec3f wi, weight;
		if (!SampleSpecularTransmission(dielectric, data, n, wo, sg, result.lobe, wi, weight, result.pdf)) return false;

		result.dir = vec3(wi.x, wi.y, wi.z);
		result.weight = vec3(weight.x, weight.y, weight.z) * (1.f - data.metallic) * data.specularTransmission / pSpecTrans;

		result.pdf *= pSpecTrans;
		if (pDielectric > 0) result.pdf += pDielectric * SampleDielectricPDF(dielectric, data, n, wo, wi);
		if (pConductor > 0.f) result.pdf += pConductor * SampleConductorPDF(conductor, data, n, wo, wi);
	} else if (pConductor > 0.f) {
		vec3f wi, weight;
		if (!SampleConductor(conductor, data, n, wo, sg, result.lobe, wi, weight, result.pdf)) return false;

		result.dir = vec3(wi.x, wi.y, wi.z);
		result.weight = vec3(weight.x, weight.y, weight.z) * data.metallic * pConductor;

		result.pdf *= pConductor;
		if (pDielectric > 0) result.pdf += pDielectric * SampleDielectricPDF(dielectric, data, n, wo, wi);
		if (pSpecTrans > 0.f) result.pdf += pSpecTrans * SampleSpecularTransmissionPDF(dielectric, data, n, wo, wi);
	}

	return true;
}

vec3 __attribute__((optnone)) EvalBSDF(
	const BSDFMaterial& data,
	const vec3& normal, const vec3& outgoing, const vec3& incoming
)
{
	vec3f dielectric{ data.dielectric.r, data.dielectric.g, data.dielectric.b };
	vec3f conductor{ data.conductor.r, data.conductor.g, data.conductor.b };
	vec3f n{ normal.x, normal.y, normal.z };
	vec3f wo{ outgoing.x, outgoing.y, outgoing.z };
	vec3f wi{ incoming.x, incoming.y, incoming.z };

	float pDielectric, pConductor, pSpecTrans;
	CalculateLobePDFs(data, pDielectric, pConductor, pSpecTrans);

	vec3f result{ 0, 0, 0 };
	if (pDielectric > 0.f) {
		result += (1.f - data.metallic) * (1.f - data.specularTransmission) * EvalDielectric(dielectric, data, n, wo, wi);
	}
	if (pConductor > 0.f) {
		result += data.metallic * EvalConductor(conductor, data, n, wo, wi);
	}
	if (pSpecTrans > 0.f) {
		result += (1.f - data.metallic) * data.specularTransmission * EvalSpecularTransmission(dielectric, data, n, wo, wi);
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

	float pDielectric, pConductor, pSpecTrans;
	CalculateLobePDFs(data, pDielectric, pConductor, pSpecTrans);

	float pdf = 0.f;
	if (pDielectric > 0.f) pdf += pDielectric * SampleDielectricPDF(dielectric, data, n, wo, wi);
	if (pConductor > 0.f) pdf += pConductor * SampleConductorPDF(conductor, data, n, wo, wi);
	if (pSpecTrans > 0.f) pdf += pSpecTrans * SampleSpecularTransmissionPDF(dielectric, data, n, wo, wi);

	return pdf;
}

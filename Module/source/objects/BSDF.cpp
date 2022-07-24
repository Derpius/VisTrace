#include "BSDF.h"

#define _USE_MATH_DEFINES
#include <math.h>

// Implementation taken from https://github.com/NVIDIAGameWorks/Falcor/blob/8c85b5a0abacc918e3c0ce2d04fa16b6ee488d3d/Source/Falcor/Rendering/Materials/BxDF.slang

using namespace glm;

static const float kMinCosTheta = 1e-6f;
static const float kMinGGXAlpha = 0.0064f;

#pragma region Utils
inline float luminance(vec3 rgb)
{
	return dot(rgb, vec3(0.2126f, 0.7152f, 0.0722f));
}

vec2 sample_disk_concentric(vec2 u)
{
	u = 2.f * u - 1.f;
	if (u.x == 0.f && u.y == 0.f) return u;
	float phi, r;
	if (abs(u.x) > abs(u.y)) {
		r = u.x;
		phi = (u.y / u.x) * M_PI_4;
	} else {
		r = u.y;
		phi = M_PI_2 - (u.x / u.y) * M_PI_4;
	}
	return r * vec2(cos(phi), sin(phi));
}

vec3 sample_cosine_hemisphere_concentric(vec2 u, float& pdf)
{
	vec2 d = sample_disk_concentric(u);
	float z = sqrt(max(0.f, 1.f - dot(d, d)));
	pdf = z * M_1_PI;
	return vec3(d, z);
}

vec3 evalFresnelSchlick(vec3 f0, vec3 f90, float cosTheta)
{
	return f0 + (f90 - f0) * pow(max(1.f - cosTheta, 0.f), 5.f); // Clamp to avoid NaN if cosTheta = 1+epsilon
}

float evalFresnelSchlick(float f0, float f90, float cosTheta)
{
	return f0 + (f90 - f0) * pow(max(1.f - cosTheta, 0.f), 5.f); // Clamp to avoid NaN if cosTheta = 1+epsilon
}

float evalFresnelDielectric(float eta, float cosThetaI, float& cosThetaT)
{
	if (cosThetaI < 0) {
		eta = 1 / eta;
		cosThetaI = -cosThetaI;
	}

	float sinThetaTSq = eta * eta * (1 - cosThetaI * cosThetaI);
	// Check for total internal reflection
	if (sinThetaTSq > 1) {
		cosThetaT = 0;
		return 1;
	}

	cosThetaT = sqrt(1 - sinThetaTSq); // No clamp needed

	// Note that at eta=1 and cosThetaI=0, we get cosThetaT=0 and NaN below.
	// It's important the framework clamps |cosThetaI| or eta to small epsilon.
	float Rs = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);
	float Rp = (eta * cosThetaT - cosThetaI) / (eta * cosThetaT + cosThetaI);

	return 0.5 * (Rs * Rs + Rp * Rp);
}

float evalFresnelDielectric(float eta, float cosThetaI)
{
	float cosThetaT;
	return evalFresnelDielectric(eta, cosThetaI, cosThetaT);
}

float evalNdfGGX(float alpha, float cosTheta)
{
	float a2 = alpha * alpha;
	float d = ((cosTheta * a2 - cosTheta) * cosTheta + 1);
	return a2 / (d * d * M_PI);
}

float evalLambdaGGX(float alphaSqr, float cosTheta)
{
	if (cosTheta <= 0.f) return 0.f;
	float cosThetaSqr = cosTheta * cosTheta;
	float tanThetaSqr = max(1.f - cosThetaSqr, 0.f) / cosThetaSqr;
	return 0.5f * (-1.f + sqrt(1.f + alphaSqr * tanThetaSqr));
}

float evalMaskingSmithGGXCorrelated(float alpha, float cosThetaI, float cosThetaO)
{
	float alphaSqr = alpha * alpha;
	float lambdaI = evalLambdaGGX(alphaSqr, cosThetaI);
	float lambdaO = evalLambdaGGX(alphaSqr, cosThetaO);
	return 1.f / (1.f + lambdaI + lambdaO);
}

float evalG1GGX(float alphaSqr, float cosTheta)
{
	if (cosTheta <= 0.f) return 0.f;
	float cosThetaSqr = cosTheta * cosTheta;
	float tanThetaSqr = max(1.f - cosThetaSqr, 0.f) / cosThetaSqr;
	return 2.f / (1.f + sqrt(1.f + alphaSqr * tanThetaSqr));
}

float evalPdfGGX_VNDF(float alpha, vec3 wi, vec3 h)
{
	float G1 = evalG1GGX(alpha * alpha, wi.z);
	float D = evalNdfGGX(alpha, h.z);
	return G1 * D * max(0.f, dot(wi, h)) / wi.z;
}

vec3 sampleGGX_VNDF(float alpha, vec3 wi, vec2 u, float& pdf)
{
	float alpha_x = alpha, alpha_y = alpha;

	// Transform the view vector to the hemisphere configuration.
	vec3 Vh = normalize(vec3(alpha_x * wi.x, alpha_y * wi.y, wi.z));

	// Construct orthonormal basis (Vh,T1,T2).
	vec3 T1 = (Vh.z < 0.9999f) ? normalize(cross(vec3(0, 0, 1), Vh)) : vec3(1, 0, 0); // TODO: fp32 precision
	vec3 T2 = cross(Vh, T1);

	// Parameterization of the projected area of the hemisphere.
	float r = sqrt(u.x);
	float phi = (2.f * M_PI) * u.y;
	float t1 = r * cos(phi);
	float t2 = r * sin(phi);
	float s = 0.5f * (1.f + Vh.z);
	t2 = (1.f - s) * sqrt(1.f - t1 * t1) + s * t2;

	// Reproject onto hemisphere.
	vec3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.f, 1.f - t1 * t1 - t2 * t2)) * Vh;

	// Transform the normal back to the ellipsoid configuration. This is our half vector.
	vec3 h = normalize(vec3(alpha_x * Nh.x, alpha_y * Nh.y, max(0.f, Nh.z)));

	pdf = evalPdfGGX_VNDF(alpha, wi, h);
	return h;
}
#pragma endregion

class DiffuseReflectionDisney
{
public:
	vec3 albedo;
	float roughness;

	vec3 eval(const vec3 wi, const vec3 wo)
	{
		if (min(wi.z, wo.z) < kMinCosTheta) return vec3(0.f);

		return evalWeight(wi, wo) * static_cast<float>(M_1_PI) * wo.z;
	}

	bool sample(const vec3 wi, vec3& wo, float& pdf, vec3& weight, uint& lobe, Sampler* sg)
	{
		wo = sample_cosine_hemisphere_concentric(sg->GetFloat2D(), pdf);
		lobe = (uint)LobeType::DiffuseReflection;

		if (min(wi.z, wo.z) < kMinCosTheta) {
			weight = {};
			return false;
		}

		weight = evalWeight(wi, wo);
		return true;
	}

	float evalPdf(const vec3 wi, const vec3 wo)
	{
		if (min(wi.z, wo.z) < kMinCosTheta) return 0.f;

		return M_1_PI * wo.z;
	}

private:
	// Returns f(wi, wo) * pi.
	vec3 evalWeight(vec3 wi, vec3 wo)
	{
		vec3 h = normalize(wi + wo);
		float woDotH = dot(wo, h);
		float fd90 = 0.5f + 2.f * woDotH * woDotH * roughness;
		float fd0 = 1.f;
		float wiScatter = evalFresnelSchlick(fd0, fd90, wi.z);
		float woScatter = evalFresnelSchlick(fd0, fd90, wo.z);
		return albedo * wiScatter * woScatter;
	}
};

class DiffuseTransmissionLambert
{
public:
	vec3 albedo;  ///< Diffuse albedo.

	vec3 eval(const vec3 wi, const vec3 wo)
	{
		if (min(wi.z, -wo.z) < kMinCosTheta) return vec3(0.f);

		return static_cast<float>(M_1_PI) * albedo * -wo.z;
	}

	bool sample(const vec3 wi, vec3& wo, float& pdf, vec3& weight, uint& lobe, Sampler* sg)
	{
		wo = sample_cosine_hemisphere_concentric(sg->GetFloat2D(), pdf);
		wo.z = -wo.z;
		lobe = (uint)LobeType::DiffuseTransmission;

		if (min(wi.z, -wo.z) < kMinCosTheta) {
			weight = {};
			return false;
		}

		weight = albedo;
		return true;
	}

	float evalPdf(const vec3 wi, const vec3 wo)
	{
		if (min(wi.z, -wo.z) < kMinCosTheta) return 0.f;

		return M_1_PI * -wo.z;
	}
};

class SpecularReflectionMicrofacet
{
public:
	vec3 albedo;      ///< Specular albedo.
	float alpha;        ///< GGX width parameter.
	uint activeLobes;   ///< BSDF lobes to include for sampling and evaluation. See LobeType.slang.

	bool hasLobe(LobeType lobe) { return (activeLobes & (uint)lobe) != 0; }

	vec3 eval(const vec3 wi, const vec3 wo)
	{
		if (min(wi.z, wo.z) < kMinCosTheta) return vec3(0.f);

		// Handle delta reflection.
		if (alpha == 0.f) return vec3(0.f);

		if (!hasLobe(LobeType::SpecularReflection)) return vec3(0.f);

		vec3 h = normalize(wi + wo);
		float wiDotH = dot(wi, h);

		float D = evalNdfGGX(alpha, h.z);
		float G = evalMaskingSmithGGXCorrelated(alpha, wi.z, wo.z);

		vec3 F = evalFresnelSchlick(albedo, vec3(1.f), wiDotH);
		return F * D * G * 0.25f / wi.z;
	}

	bool sample(const vec3 wi, vec3& wo, float pdf, vec3& weight, uint& lobe, Sampler* sg)
	{
		// Default initialization to avoid divergence at returns.
		wo = {};
		weight = {};
		pdf = 0.f;
		lobe = (uint)LobeType::SpecularReflection;

		if (wi.z < kMinCosTheta) return false;

		// Handle delta reflection.
		if (alpha == 0.f) {
			if (!hasLobe(LobeType::DeltaReflection)) return false;

			wo = vec3(-wi.x, -wi.y, wi.z);
			pdf = 0.f;
			weight = evalFresnelSchlick(albedo, vec3(1.f), wi.z);
			lobe = (uint)LobeType::DeltaReflection;
			return true;
		}

		if (!hasLobe(LobeType::SpecularReflection)) return false;

		// Sample the GGX distribution to find a microfacet normal (half vector).
		vec3 h = sampleGGX_VNDF(alpha, wi, sg->GetFloat2D(), pdf);    // pdf = G1(wi) * D(h) * max(0,dot(wi,h)) / wi.z

		// Reflect the incident direction to find the outgoing direction.
		float wiDotH = dot(wi, h);
		wo = 2.f * wiDotH * h - wi;
		if (wo.z < kMinCosTheta) return false;

		float G = evalMaskingSmithGGXCorrelated(alpha, wi.z, wo.z);
		float GOverG1wo = G * (1.f + evalLambdaGGX(alpha * alpha, wi.z));

		vec3 F = evalFresnelSchlick(albedo, vec3(1.f), wiDotH);

		pdf /= (4.f * wiDotH); // Jacobian of the reflection operator.
		weight = F * GOverG1wo;
		lobe = (uint)LobeType::SpecularReflection;
		return true;
	}

	float evalPdf(const vec3 wi, const vec3 wo)
	{
		if (min(wi.z, wo.z) < kMinCosTheta) return 0.f;

		// Handle delta reflection.
		if (alpha == 0.f) return 0.f;

		if (!hasLobe(LobeType::SpecularReflection)) return 0.f;

		vec3 h = normalize(wi + wo);
		float wiDotH = dot(wi, h);

		float pdf = evalPdfGGX_VNDF(alpha, wi, h);
		return pdf / (4.f * wiDotH);
	}
};

class SpecularReflectionTransmissionMicrofacet
{
public:
	vec3 transmissionAlbedo;  ///< Transmission albedo.
	float alpha;                ///< GGX width parameter.
	float eta;                  ///< Relative index of refraction (etaI / etaT).
	uint activeLobes;           ///< BSDF lobes to include for sampling and evaluation. See LobeType.slang.

	bool hasLobe(LobeType lobe) { return (activeLobes & (uint)lobe) != 0; }

	vec3 eval(const vec3 wi, const vec3 wo)
	{
		if (min(wi.z, abs(wo.z)) < kMinCosTheta) return vec3(0.f);

		// Handle delta reflection/transmission.
		if (alpha == 0.f) return vec3(0.f);

		const bool hasReflection = hasLobe(LobeType::SpecularReflection);
		const bool hasTransmission = hasLobe(LobeType::SpecularTransmission);
		const bool isReflection = wo.z > 0.f;
		if ((isReflection && !hasReflection) || (!isReflection && !hasTransmission)) return vec3(0.f);

		// Compute half-vector and make sure it's in the upper hemisphere.
		vec3 h = normalize(wo + wi * (isReflection ? 1.f : eta));
		h *= float(sign(h.z));

		float wiDotH = dot(wi, h);
		float woDotH = dot(wo, h);

		float D = evalNdfGGX(alpha, h.z);
		float G = evalMaskingSmithGGXCorrelated(alpha, wi.z, abs(wo.z));
		float F = evalFresnelDielectric(eta, wiDotH);

		if (isReflection) {
			return vec3(F) * D * G * 0.25f / wi.z;
		} else {
			float sqrtDenom = woDotH + eta * wiDotH;
			float t = eta * eta * wiDotH * woDotH / (wi.z * sqrtDenom * sqrtDenom);
			return transmissionAlbedo * (1.f - F) * D * G * abs(t);
		}
	}

	bool sample(const vec3 wi, vec3& wo, float& pdf, vec3& weight, uint& lobe, Sampler* sg)
	{
		// Default initialization to avoid divergence at returns.
		wo = {};
		weight = {};
		pdf = 0.f;
		lobe = (uint)LobeType::SpecularReflection;

		if (wi.z < kMinCosTheta) return false;

		// Get a random number to decide what lobe to sample.
		float lobeSample = sg->GetFloat();

		// Handle delta reflection/transmission.
		if (alpha == 0.f) {
			const bool hasReflection = hasLobe(LobeType::DeltaReflection);
			const bool hasTransmission = hasLobe(LobeType::DeltaTransmission);
			if (!(hasReflection || hasTransmission)) return false;

			float cosThetaT;
			float F = evalFresnelDielectric(eta, wi.z, cosThetaT);

			bool isReflection = hasReflection;
			if (hasReflection && hasTransmission) {
				isReflection = lobeSample < F;
			} else if (hasTransmission && F == 1.f) {
				return false;
			}

			pdf = 0.f;
			weight = isReflection ? vec3(1.f) : transmissionAlbedo;
			if (!(hasReflection && hasTransmission)) weight *= vec3(isReflection ? F : 1.f - F);
			wo = isReflection ? vec3(-wi.x, -wi.y, wi.z) : vec3(-wi.x * eta, -wi.y * eta, -cosThetaT);
			lobe = isReflection ? (uint)LobeType::DeltaReflection : (uint)LobeType::DeltaTransmission;

			if (abs(wo.z) < kMinCosTheta || (wo.z > 0.f != isReflection)) return false;

			return true;
		}

		const bool hasReflection = hasLobe(LobeType::SpecularReflection);
		const bool hasTransmission = hasLobe(LobeType::SpecularTransmission);
		if (!(hasReflection || hasTransmission)) return false;

		// Sample the GGX distribution of (visible) normals. This is our half vector.
		vec3 h = sampleGGX_VNDF(alpha, wi, sg->GetFloat2D(), pdf);    // pdf = G1(wi) * D(h) * max(0,dot(wi,h)) / wi.z

		// Reflect/refract the incident direction to find the outgoing direction.
		float wiDotH = dot(wi, h);

		float cosThetaT;
		float F = evalFresnelDielectric(eta, wiDotH, cosThetaT);

		bool isReflection = hasReflection;
		if (hasReflection && hasTransmission) {
			isReflection = lobeSample < F;
		} else if (hasTransmission && F == 1.f) {
			return false;
		}

		wo = isReflection ?
			(2.f * wiDotH * h - wi) :
			((eta * wiDotH - cosThetaT) * h - eta * wi);

		if (abs(wo.z) < kMinCosTheta || (wo.z > 0.f != isReflection)) return false;

		float woDotH = dot(wo, h);

		lobe = isReflection ? (uint)LobeType::SpecularReflection : (uint)LobeType::SpecularTransmission;

		float G = evalMaskingSmithGGXCorrelated(alpha, wi.z, abs(wo.z));
		float GOverG1wo = G * (1.f + evalLambdaGGX(alpha * alpha, wi.z));

		weight = vec3(GOverG1wo);

		if (isReflection) {
			pdf /= 4.f * woDotH; // Jacobian of the reflection operator.
		} else {
			float sqrtDenom = woDotH + eta * wiDotH;
			float denom = sqrtDenom * sqrtDenom;
			pdf = (denom > 0.f) ? pdf * abs(woDotH) / denom : FLT_MAX; // Jacobian of the refraction operator.
			weight *= transmissionAlbedo * eta * eta;
		}

		if (hasReflection && hasTransmission) {
			pdf *= isReflection ? F : 1.f - F;
		} else {
			weight *= isReflection ? F : 1.f - F;
		}

		return true;
	}

	float evalPdf(const vec3 wi, const vec3 wo)
	{
		if (min(wi.z, abs(wo.z)) < kMinCosTheta) return 0.f;

#if EnableDeltaBSDF
		// Handle delta reflection/transmission.
		if (alpha == 0.f) return 0.f;
#endif

		bool isReflection = wo.z > 0.f;
		const bool hasReflection = hasLobe(LobeType::SpecularReflection);
		const bool hasTransmission = hasLobe(LobeType::SpecularTransmission);
		if ((isReflection && !hasReflection) || (!isReflection && !hasTransmission)) return 0.f;

		// Compute half-vector and make sure it's in the upper hemisphere.
		vec3 h = normalize(wo + wi * (isReflection ? 1.f : eta));
		h *= float(sign(h.z));

		float wiDotH = dot(wi, h);
		float woDotH = dot(wo, h);

		float F = evalFresnelDielectric(eta, wiDotH);

		float pdf = evalPdfGGX_VNDF(alpha, wi, h);

		if (isReflection) {
			pdf /= 4.f * woDotH; // Jacobian of the reflection operator.
		} else {
			if (woDotH > 0.f) return 0.f;
			float sqrtDenom = woDotH + eta * wiDotH;
			float denom = sqrtDenom * sqrtDenom;
			pdf = (denom > 0.f) ? pdf * abs(woDotH) / denom : FLT_MAX; // Jacobian of the refraction operator.
		}

		if (hasReflection && hasTransmission) {
			pdf *= isReflection ? F : 1.f - F;
		}

		return pdf;
	}
};

class FalcorBSDF
{
public:
	DiffuseReflectionDisney diffuseReflection;
	DiffuseTransmissionLambert diffuseTransmission;

	SpecularReflectionMicrofacet specularReflection;
	SpecularReflectionTransmissionMicrofacet specularReflectionTransmission;

	float diffTrans;                        ///< Mix between diffuse BRDF and diffuse BTDF.
	float specTrans;                        ///< Mix between dielectric BRDF and specular BSDF.

	float pDiffuseReflection;               ///< Probability for sampling the diffuse BRDF.
	float pDiffuseTransmission;             ///< Probability for sampling the diffuse BTDF.
	float pSpecularReflection;              ///< Probability for sampling the specular BRDF.
	float pSpecularReflectionTransmission;  ///< Probability for sampling the specular BSDF.

	/** Initialize a new instance.
		\param[in] sd Shading data.
		\param[in] data BSDF parameters.
	*/
	FalcorBSDF(const MaterialProperties& data, const vec3& wo, const vec3& n)
	{
		// TODO: Currently specular reflection and transmission lobes are not properly separated.
		// This leads to incorrect behaviour if only the specular reflection or transmission lobe is selected.
		// Things work fine as long as both or none are selected.

		// Use square root if we can assume the shaded object is intersected twice.
		vec3 transmissionAlbedo = data.thin ? data.transmission : sqrt(data.transmission);

		// Setup lobes.
		diffuseReflection.albedo = data.diffuse;
		diffuseReflection.roughness = data.roughness;

		diffuseTransmission.albedo = transmissionAlbedo;

		// Compute GGX alpha.
		float alpha = data.roughness * data.roughness;

		// Alpha below min alpha value means using delta reflection/transmission.
		if (alpha < kMinGGXAlpha) alpha = 0.f;

		const uint activeLobes = data.activeLobes;

		specularReflection.albedo = data.specular;
		specularReflection.alpha = alpha;
		specularReflection.activeLobes = activeLobes;

		specularReflectionTransmission.transmissionAlbedo = transmissionAlbedo;
		// Transmission through rough interface with same IoR on both sides is not well defined, switch to delta lobe instead.
		specularReflectionTransmission.alpha = data.eta == 1.f ? 0.f : alpha;
		specularReflectionTransmission.eta = data.eta;
		specularReflectionTransmission.activeLobes = activeLobes;

		diffTrans = data.diffuseTransmission;
		specTrans = data.specularTransmission;

		// Compute sampling weights.
		float metallicBRDF = data.metallic * (1.f - specTrans);
		float dielectricBSDF = (1.f - data.metallic) * (1.f - specTrans);
		float specularBSDF = specTrans;

		float diffuseWeight = luminance(data.diffuse);
		float specularWeight = luminance(evalFresnelSchlick(data.specular, vec3(1.f), dot(wo, n)));

		pDiffuseReflection = (activeLobes & (uint)LobeType::DiffuseReflection) ? diffuseWeight * dielectricBSDF * (1.f - diffTrans) : 0.f;
		pDiffuseTransmission = (activeLobes & (uint)LobeType::DiffuseTransmission) ? diffuseWeight * dielectricBSDF * diffTrans : 0.f;
		pSpecularReflection = (activeLobes & ((uint)LobeType::SpecularReflection | (uint)LobeType::DeltaReflection)) ? specularWeight * (metallicBRDF + dielectricBSDF) : 0.f;
		pSpecularReflectionTransmission = (activeLobes & ((uint)LobeType::SpecularReflection | (uint)LobeType::DeltaReflection | (uint)LobeType::SpecularTransmission | (uint)LobeType::DeltaTransmission)) ? specularBSDF : 0.f;

		float normFactor = pDiffuseReflection + pDiffuseTransmission + pSpecularReflection + pSpecularReflectionTransmission;
		if (normFactor > 0.f) {
			normFactor = 1.f / normFactor;
			pDiffuseReflection *= normFactor;
			pDiffuseTransmission *= normFactor;
			pSpecularReflection *= normFactor;
			pSpecularReflectionTransmission *= normFactor;
		}
	}

	/** Returns the set of BSDF lobes.
		\param[in] data BSDF parameters.
		\return Returns a set of lobes (see LobeType.slang).
	*/
	static uint getLobes(const MaterialProperties& data)
	{
		float alpha = data.roughness * data.roughness;
		bool isDelta = alpha < kMinGGXAlpha;

		float diffTrans = data.diffuseTransmission;
		float specTrans = data.specularTransmission;

		uint lobes = isDelta ? (uint)LobeType::DeltaReflection : (uint)LobeType::SpecularReflection;
		if (any(greaterThan(data.diffuse, vec3(0.f))) && specTrans < 1.f) {
			if (diffTrans < 1.f) lobes |= (uint)LobeType::DiffuseReflection;
			if (diffTrans > 0.f) lobes |= (uint)LobeType::DiffuseTransmission;
		}
		if (specTrans > 0.f) lobes |= (isDelta ? (uint)LobeType::DeltaTransmission : (uint)LobeType::SpecularTransmission);

		return lobes;
	}

	vec3 eval(const vec3 wi, const vec3 wo)
	{
		vec3 result = vec3(0.f);
		if (pDiffuseReflection > 0.f) result += (1.f - specTrans) * (1.f - diffTrans) * diffuseReflection.eval(wi, wo);
		if (pDiffuseTransmission > 0.f) result += (1.f - specTrans) * diffTrans * diffuseTransmission.eval(wi, wo);
		if (pSpecularReflection > 0.f) result += (1.f - specTrans) * specularReflection.eval(wi, wo);
		if (pSpecularReflectionTransmission > 0.f) result += specTrans * (specularReflectionTransmission.eval(wi, wo));
		return result;
	}

	bool sample(const vec3 wi, vec3& wo, float& pdf, vec3& weight, uint& lobe, Sampler* sg)
	{
		// Default initialization to avoid divergence at returns.
		wo = {};
		weight = {};
		pdf = 0.f;
		lobe = (uint)LobeType::DiffuseReflection;

		bool valid = false;
		float uSelect = sg->GetFloat();

		// Note: The commented-out pdf contributions below are always zero, so no need to compute them.

		if (uSelect < pDiffuseReflection) {
			valid = diffuseReflection.sample(wi, wo, pdf, weight, lobe, sg);
			weight /= pDiffuseReflection;
			weight *= (1.f - specTrans) * (1.f - diffTrans);
			pdf *= pDiffuseReflection;
			// if (pDiffuseTransmission > 0.f) pdf += pDiffuseTransmission * diffuseTransmission.evalPdf(wi, wo);
			if (pSpecularReflection > 0.f) pdf += pSpecularReflection * specularReflection.evalPdf(wi, wo);
			if (pSpecularReflectionTransmission > 0.f) pdf += pSpecularReflectionTransmission * specularReflectionTransmission.evalPdf(wi, wo);
		} else if (uSelect < pDiffuseReflection + pDiffuseTransmission) {
			valid = diffuseTransmission.sample(wi, wo, pdf, weight, lobe, sg);
			weight /= pDiffuseTransmission;
			weight *= (1.f - specTrans) * diffTrans;
			pdf *= pDiffuseTransmission;
			// if (pDiffuseReflection > 0.f) pdf += pDiffuseReflection * diffuseReflection.evalPdf(wi, wo);
			// if (pSpecularReflection > 0.f) pdf += pSpecularReflection * specularReflection.evalPdf(wi, wo);
			if (pSpecularReflectionTransmission > 0.f) pdf += pSpecularReflectionTransmission * specularReflectionTransmission.evalPdf(wi, wo);
		} else if (uSelect < pDiffuseReflection + pDiffuseTransmission + pSpecularReflection) {
			valid = specularReflection.sample(wi, wo, pdf, weight, lobe, sg);
			weight /= pSpecularReflection;
			weight *= (1.f - specTrans);
			pdf *= pSpecularReflection;
			if (pDiffuseReflection > 0.f) pdf += pDiffuseReflection * diffuseReflection.evalPdf(wi, wo);
			// if (pDiffuseTransmission > 0.f) pdf += pDiffuseTransmission * diffuseTransmission.evalPdf(wi, wo);
			if (pSpecularReflectionTransmission > 0.f) pdf += pSpecularReflectionTransmission * specularReflectionTransmission.evalPdf(wi, wo);
		} else if (pSpecularReflectionTransmission > 0.f) {
			valid = specularReflectionTransmission.sample(wi, wo, pdf, weight, lobe, sg);
			weight /= pSpecularReflectionTransmission;
			weight *= specTrans;
			pdf *= pSpecularReflectionTransmission;
			if (pDiffuseReflection > 0.f) pdf += pDiffuseReflection * diffuseReflection.evalPdf(wi, wo);
			if (pDiffuseTransmission > 0.f) pdf += pDiffuseTransmission * diffuseTransmission.evalPdf(wi, wo);
			if (pSpecularReflection > 0.f) pdf += pSpecularReflection * specularReflection.evalPdf(wi, wo);
		}

		return valid;
	}

	float evalPdf(const vec3 wi, const vec3 wo)
	{
		float pdf = 0.f;
		if (pDiffuseReflection > 0.f) pdf += pDiffuseReflection * diffuseReflection.evalPdf(wi, wo);
		if (pDiffuseTransmission > 0.f) pdf += pDiffuseTransmission * diffuseTransmission.evalPdf(wi, wo);
		if (pSpecularReflection > 0.f) pdf += pSpecularReflection * specularReflection.evalPdf(wi, wo);
		if (pSpecularReflectionTransmission > 0.f) pdf += pSpecularReflectionTransmission * specularReflectionTransmission.evalPdf(wi, wo);
		return pdf;
	}
};

vec3 toLocal(const vec3& T, const vec3& B, const vec3& N, const vec3& v)
{
	return vec3(dot(v, T), dot(v, B), dot(v, N));
}
vec3 fromLocal(const vec3& T, const vec3& B, const vec3& N, const vec3& v)
{
	return T * v.x + B * v.y + N * v.z;
}

bool SampleFalcorBSDF(
	const MaterialProperties& data, Sampler* sg, BSDFSample& result,
	const vec3& normal, const vec3& tangent, const vec3& binormal,
	const vec3& toEye
)
{
	vec3 wiLocal = toLocal(tangent, binormal, normal, toEye);
	vec3 woLocal = {};

	FalcorBSDF bsdf = FalcorBSDF(data, toEye, normal);

	bool valid = bsdf.sample(wiLocal, woLocal, result.pdf, result.weight, result.lobe, sg);
	result.wo = fromLocal(tangent, binormal, normal, woLocal);

	return valid;
}

vec3 EvalFalcorBSDF(
	const MaterialProperties& data,
	const vec3& normal, const vec3& tangent, const vec3& binormal,
	const vec3& toEye, const vec3& sampledDir
)
{
	vec3 wiLocal = toLocal(tangent, binormal, normal, toEye);
	vec3 woLocal = toLocal(tangent, binormal, normal, sampledDir);

	FalcorBSDF bsdf = FalcorBSDF(data, toEye, normal);

	return bsdf.eval(wiLocal, woLocal);
}

float EvalPDFFalcorBSDF(
	const MaterialProperties& data,
	const vec3& normal, const vec3& tangent, const vec3& binormal,
	const vec3& toEye, const vec3& sampledDir
)
{
	vec3 wiLocal = toLocal(tangent, binormal, normal, toEye);
	vec3 woLocal = toLocal(tangent, binormal, normal, sampledDir);

	FalcorBSDF bsdf = FalcorBSDF(data, toEye, normal);

	return bsdf.evalPdf(wiLocal, woLocal);
}

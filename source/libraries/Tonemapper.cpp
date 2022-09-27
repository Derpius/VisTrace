#include "Tonemapper.h"

#ifdef _OPENMP
#include <omp.h>
#endif

#include "glm/glm.hpp"
#include "glm/gtx/compatibility.hpp"
using namespace glm;

#include "RenderTarget.h"

using namespace VisTrace;

// All credit to
// https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
// https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/Exposure.hlsl
// https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ToneMapping.hlsl

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
static const mat3x3 ACESInputMat = {
	{0.59719, 0.35458, 0.04823},
	{0.07600, 0.90834, 0.01566},
	{0.02840, 0.13383, 0.83777}
};

// ODT_SAT => XYZ => D60_2_D65 => sRGB
static const mat3x3 ACESOutputMat = {
	{ 1.60475, -0.53108, -0.07367},
	{-0.10208,  1.10813, -0.00605},
	{-0.00327, -0.07276,  1.07602}
};

inline vec3 RRTAndODTFit(const vec3& v)
{
	vec3 a = v * (v + 0.0245786f) - 0.000090537f;
	vec3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
	return a / b;
}

inline vec3 ACESFitted(vec3 colour)
{
	colour = colour * ACESInputMat;
	colour = RRTAndODTFit(colour);
	colour = colour * ACESOutputMat;
	return saturate(colour);
}

inline float CalcExposure(float avgLuminance, const float offset = 0.f)
{
	avgLuminance = max(avgLuminance, 0.00001f);
	float linearExposure = (0.264f / avgLuminance); // https://www.desmos.com/calculator/nzwwfw96fb
	float exposure = log2(max(linearExposure, 0.00001f));

	exposure += offset;
	return exp2(exposure);
}

inline vec3 LinearTosRGB(const vec3& colour)
{
	vec3 x = colour * 12.92f;
	vec3 y = 1.055f * pow(saturate(colour), vec3(1.0f / 2.4f)) - 0.055f;

	return vec3(
		colour.r < 0.0031308f ? x.r : y.r,
		colour.g < 0.0031308f ? x.g : y.g,
		colour.b < 0.0031308f ? x.b : y.b
	);
}

void Tonemap(IRenderTarget* pRt, const bool autoExposure, const float autoExposureOffset)
{
	if (pRt->GetFormat() != RTFormat::RGBFFF) return;
	uint16_t width = pRt->GetWidth(), height = pRt->GetHeight();

	vec3* pData = reinterpret_cast<vec3*>(pRt->GetRawData());
	size_t size = width * height;

	if (autoExposure) {
		double totalLuminance = 0.0;

		#pragma omp parallel for reduction (+:totalLuminance)
		for (size_t ptr = 0; ptr < size; ptr++) {
			totalLuminance += dot(pData[ptr], vec3(0.2126f, 0.7152f, 0.0722f));
		}

		const float avgLuminance = totalLuminance / static_cast<double>(size);
		const float exposure = CalcExposure(avgLuminance, autoExposureOffset);

		#pragma omp parallel for
		for (size_t ptr = 0; ptr < size; ptr++) {
			pData[ptr] = LinearTosRGB(ACESFitted(exposure * pData[ptr]));
		}
	} else {
		#pragma omp parallel for
		for (size_t ptr = 0; ptr < size; ptr++) {
			pData[ptr] = LinearTosRGB(ACESFitted(pData[ptr]));
		}
	}
}

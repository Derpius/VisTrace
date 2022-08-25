#include "Tonemapper.h"

#include <omp.h>

#include "glm/glm.hpp"
#include "glm/gtx/compatibility.hpp"
using namespace glm;

#include "vistrace/IRenderTarget.h"
using namespace VisTrace;

// All credit to https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl

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

void Tonemap(IRenderTarget* pRt)
{
	if (pRt->GetFormat() != RTFormat::RGBFFF) return;
	vec3* pData = reinterpret_cast<vec3*>(pRt->GetRawData());
	size_t size = pRt->GetWidth() * pRt->GetHeight();

	#pragma omp parallel for
	for (size_t ptr = 0; ptr < size; ptr++) {
		pData[ptr] = ACESFitted(pData[ptr]);
	}
}

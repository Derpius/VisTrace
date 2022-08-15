#include "HDRI.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define _USE_MATH_DEFINES
#include <math.h>

#include "glm/gtx/euler_angles.hpp"

int HDRI::id{ -1 };

constexpr float M_1_4PI = 1.0 / (4.0 * M_PI);

// https://learnopengl.com/PBR/IBL/Diffuse-irradiance
static glm::vec2 invAtan{ .1591f, .3183f };
glm::uvec2 EquirectangularDirToTexel(glm::vec3 direction, int resX, int resY)
{
	glm::vec2 uv = glm::vec2(glm::atan(direction.y, direction.x), glm::asin(-direction.z));
	uv *= invAtan;
	uv += .5f;

	return glm::uvec2(uv.x * resX, uv.y * resY);
}

// https://gamedev.stackexchange.com/questions/169508/octahedral-impostors-octahedral-mapping
glm::vec3 OctahedralTexelToDir(glm::vec2 texel)
{
	using namespace glm;

	texel.y = 1.f - texel.y;

	vec3 position = vec3(2.f * (texel - .5f), 0.f);

	vec2 absolute = abs(vec2(position));
	position.z = 1.f - absolute.x - absolute.y;

	if (position.z < 0.f) {
		position = vec3(
			sign(vec2(position)) * vec2(1.f - absolute.y, 1.f - absolute.x),
			position.z
		);
	}

	return normalize(position);
}

// https://gamedev.stackexchange.com/questions/169508/octahedral-impostors-octahedral-mapping
glm::vec2 OctahedralDirToTexel(glm::vec3 direction)
{
	using namespace glm;

	vec3 octant = sign(direction);

	float sum = dot(direction, octant);
	vec3 octahedron = direction / sum;

	if (octahedron.z < 0.f) {
		vec3 absolute = abs(octahedron);
		octahedron = vec3(
			vec2(octant) * vec2(1.f - absolute.y, 1.f - absolute.x),
			octahedron.z
		);
	}

	vec2 uv = vec2(octahedron) * .5f + .5f;
	uv.y = 1.f - uv.y;

	return uv;
}

HDRI::HDRI(
	const uint8_t* pFileData, const size_t size,
	const uint16_t importanceRes, const uint16_t importanceSamples
) : mImportanceRes(importanceRes), mImportanceSamples(importanceSamples)
{
	// Load equirectangular image (TODO: handle errors from stb_image)
	mpData = stbi_loadf_from_memory(pFileData, size, &mResX, &mResY, &mChannels, 0);
	if (mpData == nullptr) return;

	if (mResX <= 0 || mResY <= 0) {
		free(mpData);
		mpData = nullptr;
		return;
	}

	// Set up importance map
	// https://github.com/NVIDIAGameWorks/Falcor/blob/master/Source/Falcor/Rendering/Lights/EnvMapSampler.cpp
	// https://github.com/NVIDIAGameWorks/Falcor/blob/master/Source/Falcor/Rendering/Lights/EnvMapSamplerSetup.cs.slang
	mImportanceBaseMip = log2(mImportanceRes);
	uint8_t mips = mImportanceBaseMip + 1;
	mpImportanceMap = new RT::Texture(mImportanceRes, mImportanceRes, RT::Format::RF, mips);
	if (mpImportanceMap == nullptr || !mpImportanceMap->IsValid()) {
		free(mpData);
		mpData = nullptr;
		return;
	}

	uint16_t samplesX = std::max(static_cast<uint16_t>(1), static_cast<uint16_t>(std::sqrt(mImportanceSamples)));
	uint16_t samplesY = mImportanceSamples / samplesX;

	glm::uvec2 outputDim(mImportanceRes, mImportanceRes);
	glm::uvec2 outputDimInSamples(mImportanceRes * samplesX, mImportanceRes * samplesY);
	glm::uvec2 numSamples(samplesX, samplesY);
	float invSamples = 1.f / (samplesX * samplesY);
	mImportanceInvDim = 1.f / glm::vec2(mImportanceRes, mImportanceRes);

	// Calculate angles
	mAngle = glm::eulerAngleZYX(mAngleEuler.y, mAngleEuler.x, mAngleEuler.z);
	mAngleInverse = glm::inverse(mAngle);

	// Calculate luminance and save to importance map
	for (uint16_t y = 0; y < mImportanceRes; y++) {
		for (uint16_t x = 0; x < mImportanceRes; x++) {
			float luminance = 0.f;

			for (uint16_t sY = 0; sY < samplesY; sY++) {
				for (uint16_t sX = 0; sX < samplesX; sX++) {
					glm::uvec2 samplePos = glm::uvec2(x, y) * numSamples + glm::uvec2(sX, sY);
					glm::vec2 p = (glm::vec2(samplePos) + 0.5f) / glm::vec2(outputDimInSamples);

					glm::vec3 dir = OctahedralTexelToDir(p);
					glm::vec2 equiTexel = EquirectangularDirToTexel(dir, mResX, mResY);
					size_t equiOffset = (equiTexel.y * mResX + equiTexel.x) * mChannels;

					glm::vec3 radiance(
						mpData[equiOffset],
						mpData[equiOffset + 1U],
						mpData[equiOffset + 2U]
					);
					luminance += glm::dot(glm::vec3(radiance.r, radiance.g, radiance.b), glm::vec3(0.299f, 0.587f, 0.114f));
				}
			}

			mpImportanceMap->SetPixel(x, y, RT::Pixel{ luminance * invSamples });
		}
	}

	// Generate importance map mips
	mpImportanceMap->GenerateMIPs();
}

HDRI::~HDRI()
{
	if (mpData != nullptr) free(mpData);
	if (mpImportanceMap != nullptr) delete mpImportanceMap;
}

bool HDRI::IsValid() const
{
	return (
		mpData != nullptr && mpImportanceMap != nullptr &&
		mpImportanceMap->IsValid() &&
		mResX > 0 && mResY > 0
	);
}

glm::vec3 HDRI::GetPixel(glm::vec3 direction) const
{
	if (!IsValid()) return glm::vec3();

	glm::uvec2 texel = EquirectangularDirToTexel(mAngleInverse * glm::vec4(direction, 0.f), mResX, mResY);
	size_t equiOffset = (texel.y * mResX + texel.x) * mChannels;

	return glm::vec3(
		mpData[equiOffset],
		mpData[equiOffset + 1U],
		mpData[equiOffset + 2U]
	);
}

float HDRI::EvalPDF(glm::vec3 direction) const
{
	if (!IsValid()) return 0.f;

	glm::vec2 uv = OctahedralDirToTexel(mAngleInverse * glm::vec4(direction, 0.f));
	float averageLuminance = mpImportanceMap->GetPixel(0, 0, mImportanceBaseMip).r;
	float pdf = mpImportanceMap->GetPixel(uv.x * mImportanceRes, uv.y * mImportanceRes, 0).r / averageLuminance;
	return pdf * M_1_4PI;
}

// https://github.com/NVIDIAGameWorks/Falcor/blob/master/Source/Falcor/Rendering/Lights/EnvMapSampler.slang
bool HDRI::Sample(float& pdf, glm::vec3& sampleDir, glm::vec3& colour, Sampler* sg) const
{
	using namespace glm;

	if (!IsValid()) {
		pdf = 0.f;
		sampleDir = vec3(0.f);
		colour = vec3(0.f);
		return false;
	}

	vec2 p = sg->GetFloat2D();
	uvec2 pos = uvec2(0);

	for (int mip = mImportanceBaseMip - 1; mip >= 0; mip--) {
		pos *= 2;

		float w[4] = {
			mpImportanceMap->GetPixel(pos.x    , pos.y    , mip).r,
			mpImportanceMap->GetPixel(pos.x + 1, pos.y    , mip).r,
			mpImportanceMap->GetPixel(pos.x    , pos.y + 1, mip).r,
			mpImportanceMap->GetPixel(pos.x + 1, pos.y + 1, mip).r
		};

		float q[2] = {
			w[0] + w[2],
			w[1] + w[3]
		};

		uvec2 off;

		float d = q[0] / (q[0] + q[1]);

		if (p.x < d) {
			off.x = 0;
			p.x = p.x / d;
		} else {
			off.x = 1;
			p.x = (p.x - d) / (1.f - d);
		}

		float e = off.x == 0 ? (w[0] / q[0]) : (w[1] / q[1]);

		if (p.y < e) {
			off.y = 0;
			p.y = p.y / e;
		} else {
			off.y = 1;
			p.y = (p.y - e) / (1.f - e);
		}

		pos += off;
	}

	vec2 uv = (static_cast<vec2>(pos) + p) * mImportanceInvDim;
	sampleDir = OctahedralTexelToDir(uv);

	float averageLuminance = mpImportanceMap->GetPixel(0, 0, mImportanceBaseMip).r;
	pdf = mpImportanceMap->GetPixel(uv.x * mImportanceRes, uv.y * mImportanceRes, 0).r / averageLuminance;
	pdf *= M_1_4PI;

	colour = GetPixel(sampleDir);

	return true;
}

void HDRI::SetAngle(const glm::vec3& angle)
{
	mAngleEuler = glm::radians(angle);
	mAngle = glm::eulerAngleZYX(mAngleEuler.y, mAngleEuler.x, mAngleEuler.z);
	mAngleInverse = glm::inverse(mAngle);
}

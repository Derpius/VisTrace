#include "HDRI.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define _USE_MATH_DEFINES
#include <math.h>

#include "glm/gtx/euler_angles.hpp"

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
glm::vec3 HDRI::TexelToDir(glm::vec2 texel) const
{
	using namespace glm;

	vec2 uv = texel / vec2(mRes, mRes);
	uv.y = 1.f - uv.y;

	vec3 position = vec3(2.f * (uv - .5f), 0.f);

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
glm::uvec2 HDRI::DirToTexel(glm::vec3 direction) const
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

	return uvec2(uv * vec2(mRes));
}

// http://karim.naaji.fr/environment_map_importance_sampling.html
void HDRI::ProcessBins(float radiancePrev, Bin lastBin)
{
	using namespace glm;

	uint32_t w = lastBin.x1 - lastBin.x0;
	uint32_t h = lastBin.y1 - lastBin.y0;

	if (radiancePrev <= mRadianceThreshold || w * h < mAreaThreshold) {
		mSampleBins.push_back(lastBin);
		return;
	}

	bool verticalSplit = w > h;
	uint32_t xsplit = verticalSplit ? w / 2.f + lastBin.x0 : lastBin.x1;
	uint32_t ysplit = verticalSplit ? lastBin.y1 : h / 2.f + lastBin.y0;

	float radianceCurr = 0.f;
	for (uint32_t x = lastBin.x0; x < xsplit; x++) {
		for (uint32_t y = lastBin.y0; y < ysplit; y++) {
			radianceCurr += mpImageData[(y * mRes + x) * 4 + 3];
		}
	}

	ProcessBins(radianceCurr, Bin{ lastBin.x0, lastBin.y0, xsplit, ysplit });

	float radianceNew = radiancePrev - radianceCurr;
	if (verticalSplit)
		ProcessBins(radianceNew, Bin{ xsplit, lastBin.y0, lastBin.x1, lastBin.y1 });
	else
		ProcessBins(radianceNew, Bin{ lastBin.x0, ysplit, lastBin.x1, lastBin.y1 });
}

void HDRI::CachePDF()
{
	using namespace glm;

	size_t numBins = mSampleBins.size();
	for (const auto& bin : mSampleBins) {
		uint32_t w = bin.x1 - bin.x0;
		uint32_t h = bin.y1 - bin.y0;

		for (uint32_t x = bin.x0; x < bin.x1; x++) {
			for (uint32_t y = bin.y0; y < bin.y1; y++) {
				vec2 uv = vec2(static_cast<float>(x) / static_cast<float>(mRes), static_cast<float>(y) / static_cast<float>(mRes));

				float binPdf = (mRes * mRes) / (numBins * w * h);
				float pdf = binPdf * M_1_4PI;

				mpPdfData[y * mRes + x] = pdf;
			}
		}
	}
}

HDRI::HDRI(
	const uint8_t* pFileData, const size_t size,
	const float radianceThreshold, const uint32_t areaThreshold
) : mRadianceThreshold(radianceThreshold), mAreaThreshold(areaThreshold)
{
	// Load equirectangular image (TODO: handle errors from stb_image)
	int resX, resY;
	float* pEquiData = stbi_loadf_from_memory(pFileData, size, &resX, &resY, &mChannels, 4);
	if (pEquiData == nullptr) return;

	mRes = static_cast<uint32_t>(resY);
	mpImageData = reinterpret_cast<float*>(malloc(sizeof(float) * mRes * mRes * 4U));

	// Sampler containers
	mSampleBins = std::vector<Bin>();
	mpPdfData = reinterpret_cast<float*>(malloc(sizeof(float) * mRes * mRes));

	// Calculate angles
	mAngle = glm::eulerAngleZYX(mAngleEuler.y, mAngleEuler.x, mAngleEuler.z);
	mAngleInverse = glm::inverse(mAngle);

	if (mRes <= 0) return;

	// Calculate luminance and convert to octahedral mapping
	float luminance = 0.f;
	uint32_t x = 0U, y = 0U;
	for (float* r = mpImageData; r < mpImageData + mRes * mRes * 4U; r += 4) {
		// Get other components
		float* g = r + 1;
		float* b = r + 2;
		float* l = r + 3;

		// Get pixel from equirectangular map
		glm::vec3 dir = TexelToDir(glm::uvec2(x, y));
		glm::uvec2 equiTexel = EquirectangularDirToTexel(dir, resX, resY);
		size_t equiOffset = (equiTexel.y * resX + equiTexel.x) * 4U;
		*r = pEquiData[equiOffset];
		*g = pEquiData[equiOffset + 1U];
		*b = pEquiData[equiOffset + 2U];

		// Calculate luminance
		*l = glm::dot(glm::vec3(*r, *g, *b), glm::vec3(0.299f, 0.587f, 0.114f));
		luminance += *l;

		// Increment coords
		if (++x >= mRes) {
			x = 0;
			y++;
		}
	}

	ProcessBins(luminance, Bin{ 0, 0, mRes, mRes });
	CachePDF();

	free(pEquiData);
}

HDRI::~HDRI()
{
	if (mpImageData != nullptr) free(mpImageData);
	if (mpPdfData != nullptr) free(mpPdfData);
}

bool HDRI::IsValid() const
{
	return (
		mpImageData != nullptr &&
		mRes > 0 &&
		mSampleBins.size() > 0
	);
}

glm::vec4 HDRI::GetPixel(glm::vec3 direction) const
{
	if (!IsValid()) return glm::vec4();

	glm::uvec2 texel = DirToTexel(mAngleInverse * glm::vec4(direction, 0.f));
	size_t offset = (texel.y * mRes + texel.x) * 4;

	return glm::vec4(
		mpImageData[offset],
		mpImageData[offset + 1],
		mpImageData[offset + 2],
		mpImageData[offset + 3]
	);
}

float HDRI::EvalPDF(glm::vec3 direction) const
{
	if (!IsValid()) return 0.f;

	glm::uvec2 texel = DirToTexel(mAngleInverse * glm::vec4(direction, 0.f));
	return mpPdfData[texel.y * mRes + texel.x];
}

// http://karim.naaji.fr/environment_map_importance_sampling.html
bool HDRI::Sample(float& pdf, glm::vec3& sampleDir, glm::vec3& colour, Sampler* sg) const
{
	using namespace glm;

	if (!IsValid()) {
		pdf = 0.f;
		sampleDir = vec3(0.f);
		colour = vec3(0.f);
		return false;
	}

	float numBins = mSampleBins.size();
	if (numBins == 0.f) {
		pdf = 0.f;
		sampleDir = vec3(0.f);
		colour = vec3(0.f);
		return false;
	}

	size_t binIndex = numBins * sg->GetFloat();
	if (binIndex >= numBins) binIndex = mSampleBins.size() - 1U;
	const Bin& bin = mSampleBins[binIndex];

	float binWidth = bin.x1 - bin.x0;
	float binHeight = bin.y1 - bin.y0;

	// Generate a random sample from within the bin
	vec2 uv{
		binWidth * sg->GetFloat() + bin.x0,
		binHeight * sg->GetFloat() + bin.y0
	};

	uvec2 texel = floor(uv);
	sampleDir = mAngle * vec4(TexelToDir(texel), 0.f);

	uint32_t offset = texel.y * mRes + texel.x;
	pdf = mpPdfData[offset];

	offset *= 4;
	colour = glm::vec3(
		mpImageData[offset],
		mpImageData[offset + 1],
		mpImageData[offset + 2]
	);
	return pdf > 0.f;
}

void HDRI::SetAngle(const glm::vec3& angle)
{
	mAngleEuler = glm::radians(angle);
	mAngle = glm::eulerAngleZYX(mAngleEuler.y, mAngleEuler.x, mAngleEuler.z);
	mAngleInverse = glm::inverse(mAngle);
}

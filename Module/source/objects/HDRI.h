#pragma once

#include <cstdint>
#include "glm/glm.hpp"

#include <vector>

#include "Sampler.h"

struct Bin
{
	uint32_t x0;
	uint32_t y0;
	uint32_t x1;
	uint32_t y1;
};

class HDRI
{
private:
	float* mpImageData;
	float* mpPdfData;

	std::vector<Bin> mSampleBins;
	float mRadianceThreshold;
	uint32_t mAreaThreshold;

	uint32_t mRes = 0;
	int32_t mChannels = 0;

	glm::vec3 mAngleEuler = glm::vec3(0.f);
	glm::mat4 mAngle;
	glm::mat4 mAngleInverse;

	glm::vec3 TexelToDir(glm::vec2 texel) const;
	glm::uvec2 DirToTexel(glm::vec3 direction) const;

	void ProcessBins(float radiancePrev, Bin lastBin);

	void CachePDF();

public:
	HDRI(const uint8_t* pFileData, const size_t size, const float radianceThreshold, const uint32_t areaThreshold);
	~HDRI();

	bool IsValid() const;

	glm::vec4 GetPixel(glm::vec3 direction) const;

	float EvalPDF(glm::vec3 direction) const;

	bool Sample(float& pdf, glm::vec3& sampleDir, glm::vec3& colour, Sampler* sg) const;

	void SetAngle(const glm::vec3& angle);
};

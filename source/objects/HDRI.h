#pragma once

#include <cstdint>
#include "glm/glm.hpp"

#include <vector>

#include "Sampler.h"
#include "RenderTarget.h"

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
	float* mpData;
	int mResX = 0, mResY = 0, mChannels = 0;

	uint16_t mImportanceRes, mImportanceSamples;
	uint8_t mImportanceBaseMip = 0;
	glm::vec2 mImportanceInvDim;
	RT::Texture* mpImportanceMap;

	glm::vec3 mAngleEuler = glm::vec3(0.f);
	glm::mat4 mAngle;
	glm::mat4 mAngleInverse;

public:
	static int id;

	HDRI(const uint8_t* pFileData, const size_t size, const uint16_t importanceRes, const uint16_t importanceSamples);
	~HDRI();

	bool IsValid() const;

	glm::vec3 GetPixel(glm::vec3 direction) const;

	float EvalPDF(glm::vec3 direction) const;

	bool Sample(float& pdf, glm::vec3& sampleDir, glm::vec3& colour, Sampler* sg) const;

	void SetAngle(const glm::vec3& angle);
};

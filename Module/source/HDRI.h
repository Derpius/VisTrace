#pragma once

#include <cstdint>
#include "glm/glm.hpp"

class HDRI
{
private:
	float* mpImageData;

	int32_t mResX = 0;
	int32_t mResY = 0;
	int32_t mChannels = 0;

	glm::vec3 mAngleEuler = glm::vec3(0.f);
	glm::mat4 mAngle;
	glm::mat4 mAngleInverse;

public:
	HDRI(const uint8_t* pFileData, const size_t size);
	~HDRI();

	bool IsValid() const;

	glm::vec4 GetPixel(glm::vec3 direction) const;

	glm::vec3 Sample(float& pdf) const;

	void SetAngle(const glm::vec3& angle);
};

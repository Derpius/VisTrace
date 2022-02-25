#include "HDRI.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define _USE_MATH_DEFINES
#include <math.h>

#include "glm/gtx/euler_angles.hpp"

HDRI::HDRI(const uint8_t* pFileData, const size_t size)
{
	mpImageData = stbi_loadf_from_memory(pFileData, size, &mResX, &mResY, &mChannels, 4);
	mAngle = glm::eulerAngleXYZ(mAngleEuler.x, mAngleEuler.y, mAngleEuler.z);
	mAngleInverse = glm::inverse(mAngle);
}

HDRI::~HDRI()
{
	if (mpImageData != nullptr) free(mpImageData);
}

bool HDRI::IsValid() const
{
	return mpImageData != nullptr;
}

// https://learnopengl.com/PBR/IBL/Diffuse-irradiance
static glm::vec2 invAtan{ .1591f, .3183f };
glm::vec4 HDRI::GetPixel(glm::vec3 direction) const
{
	if (mpImageData == nullptr)
		return glm::vec4();

	direction = mAngle * glm::vec4(direction, 0.f);

	glm::vec2 uv = glm::vec2(glm::atan(direction.y, direction.x), glm::asin(-direction.z));
	uv *= invAtan;
	uv += .5f;

	int32_t x = floor(uv.x * mResX), y = floor(uv.y * mResY);
	size_t offset = (y * mResX + x) * 4;

	return glm::vec4(
		mpImageData[offset],
		mpImageData[offset + 1],
		mpImageData[offset + 2],
		mpImageData[offset + 3]
	);
}

glm::vec3 HDRI::Sample(float& pdf) const
{
	//if (mpImageData == nullptr)
		pdf = 0.f;
		return glm::vec3();
}

void HDRI::SetAngle(const glm::vec3& angle)
{
	mAngleEuler = angle;
	mAngle = glm::eulerAngleXYZ(angle.x, angle.y, angle.z);
	mAngleInverse = glm::inverse(mAngle);
}

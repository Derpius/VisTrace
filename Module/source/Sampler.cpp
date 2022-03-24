#include "Sampler.h"

Sampler::Sampler(uint32_t seed)
{
	mGenerator = std::mt19937(seed);
	mDistribution = std::uniform_real_distribution<double>(0., 1.);
}

float Sampler::GetFloat()
{
	return static_cast<float>(mDistribution(mGenerator));
}

glm::vec2 Sampler::GetFloat2D()
{
	return glm::vec2(
		static_cast<float>(mDistribution(mGenerator)),
		static_cast<float>(mDistribution(mGenerator))
	);
}

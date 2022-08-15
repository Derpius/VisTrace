#include "Sampler.h"

int Sampler::id{ -1 };

Sampler::Sampler(uint32_t seed)
{
	mGenerator = std::mt19937(seed);
	mDistribution = std::uniform_real_distribution<double>(0., 1.);
}

float Sampler::GetFloat()
{
	return static_cast<float>(mDistribution(mGenerator));
}

void Sampler::GetFloat2D(float& r1, float& r2)
{
	r1 = mDistribution(mGenerator);
	r2 = mDistribution(mGenerator);
}

#pragma once

#include <random>
#include "glm/glm.hpp"

#include "vistrace/ISampler.h"

class Sampler : public VisTrace::ISampler
{
private:
	std::mt19937 mGenerator;
	std::uniform_real_distribution<double> mDistribution;

public:
	static int id;

	Sampler(uint32_t seed);

	float GetFloat();
	void GetFloat2D(float& r1, float& r2);
};
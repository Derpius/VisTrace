#pragma once

#include <random>
#include "glm/glm.hpp"

class Sampler
{
private:
	std::mt19937 mGenerator;
	std::uniform_real_distribution<double> mDistribution;

public:
	Sampler(uint32_t seed);

	float GetFloat();
	glm::vec2 GetFloat2D();
};
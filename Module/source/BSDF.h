#pragma once

#include "glm/glm.hpp"

#include <random>

class BSDFSampler
{
private:
	std::mt19937 mGenerator;
	std::uniform_real_distribution<double> mDistribution;

public:
	BSDFSampler(uint32_t seed);

	float GetFloat();
	glm::vec2 GetFloat2D();
};

enum class LobeType : glm::uint
{
	None = 0x00,

	DiffuseReflection = 0x01,
	SpecularReflection = 0x02,
	DeltaReflection = 0x04,

	DiffuseTransmission = 0x10,
	SpecularTransmission = 0x20,
	DeltaTransmission = 0x40,

	Diffuse = 0x11,
	Specular = 0x22,
	Delta = 0x44,
	NonDelta = 0x33,

	Reflection = 0x0f,
	Transmission = 0xf0,

	NonDeltaReflection = 0x03,
	NonDeltaTransmission = 0x30,

	All = 0xff,
};

struct MaterialProperties
{
	glm::vec3 diffuse{ .3f };
	glm::vec3 specular{ 1.f };
	glm::vec3 transmission{ 1.f };

	float roughness = 1.f;
	float metallic = 0.f;

	float eta = 1.f;
	float diffuseTransmission = 0.f;
	float specularTransmission = 0.f;

	bool thin = false;

	glm::uint activeLobes = static_cast<glm::uint>(LobeType::All);
};

struct BSDFSample
{
	glm::vec3 wo;
	float pdf;
	glm::vec3 weight;
	glm::uint lobe;
};

/// <summary>
/// Samples the Falcor BSDF
/// </summary>
/// <param name="data">Material data</param>
/// <param name="sg">Sample generator</param>
/// <param name="result">Sample result</param>
/// <param name="normal">Normal at hit point</param>
/// <param name="tangent">Tangent at hit point</param>
/// <param name="binormal">Binormal at hit point</param>
/// <param name="toEye">Vector towards camera or previous hit</param>
/// <returns>Whether the sample is valid</returns>
bool SampleFalcorBSDF(
	const MaterialProperties& data, BSDFSampler* sg, BSDFSample& result,
	const glm::vec3& normal, const glm::vec3& tangent, const glm::vec3& binormal,
	const glm::vec3& toEye
);

/// <summary>
/// Evaluates the Falcor BSDF
/// </summary>
/// <param name="data">Material data</param>
/// <param name="normal">Normal at hit point</param>
/// <param name="tangent">Tangent at hit point</param>
/// <param name="binormal">Binormal at hit point</param>
/// <param name="toEye">Vector towards camera or previous hit</param>
/// <param name="sampledDir">Sampled vector at this hit</param>
/// <returns>The value of the BSDF</returns>
glm::vec3 EvalFalcorBSDF(
	const MaterialProperties& data,
	const glm::vec3& normal, const glm::vec3& tangent, const glm::vec3& binormal,
	const glm::vec3& toEye, const glm::vec3& sampledDir
);

/// <summary>
/// Evaluates the Falcor BSDF's PDF
/// </summary>
/// <param name="data">Material data</param>
/// <param name="normal">Normal at hit point</param>
/// <param name="tangent">Tangent at hit point</param>
/// <param name="binormal">Binormal at hit point</param>
/// <param name="toEye">Vector towards camera or previous hit</param>
/// <param name="sampledDir">Sampled vector at this hit</param>
/// <returns>The value of the PDF</returns>
float EvalPDFFalcorBSDF(
	const MaterialProperties& data,
	const glm::vec3& normal, const glm::vec3& tangent, const glm::vec3& binormal,
	const glm::vec3& toEye, const glm::vec3& sampledDir
);

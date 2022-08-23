#pragma once

#include "glm/glm.hpp"

#include "Sampler.h"

// Most significant bit of each nibble reserved 
enum class LobeType : uint8_t
{
	None = 0,

	DiffuseReflection           = 0b00000001,
	DiffuseTransmission         = 0b00000010,
	DielectricReflection        = 0b00000100,
	DielectricTransmission      = 0b00001000,
	ConductiveReflection        = 0b00010000,
	DeltaDielectricReflection   = 0b00100000,
	DeltaDielectricTransmission = 0b01000000,
	DeltaConductiveReflection   = 0b10000000,

	Reflection                  = 0b10110101,
	Transmission                = 0b01001010,

	Delta                       = 0b11100000,
	NonDelta                    = 0b00011111,

	Diffuse                     = 0b00000011,
	Specular                    = 0b11111100,
	SpecularDielectric          = 0b01101100,
	SpecularConductive          = 0b10010000,

	All = 0b11111111
};

struct BSDFMaterial
{
	static int id;

	glm::vec3 baseColour{ 1.f, 1.f, 1.f };
	float ior = 1.5;

	bool roughnessOverridden = false;
	float roughness = 1.f;
	float linearRoughness = 1.f;
	bool metallicOverridden = false;
	float metallic = 0.f;

	glm::vec3 dielectric{ 1.f, 1.f, 1.f };
	glm::vec3 conductor{ 1.f, 1.f, 1.f };

	float diffuseTransmission = 0.f;
	float specularTransmission = 0.f;

	bool thin = false;

	void PrepShadingData(
		const glm::vec3& hitColour, float hitMetalness, float hitRoughness,
		bool frontFacing
	);
};

struct BSDFSample
{
	glm::vec3 dir = glm::vec3(0.f);
	float pdf = 0.f;
	glm::vec3 weight = glm::vec3(0.f);
	LobeType lobe = LobeType::None;
};

/// <summary>
/// Samples the BSDF
/// </summary>
/// <param name="data">Material data</param>
/// <param name="sg">Sample generator</param>
/// <param name="result">Sample result</param>
/// <param name="normal">Normal at hit point</param>
/// <param name="outgoing">Vector towards camera or previous hit</param>
/// <returns>Whether the sample is valid</returns>
bool SampleBSDF(
	const BSDFMaterial& data, Sampler* sg,
	const glm::vec3& normal, const glm::vec3& outgoing,
	BSDFSample& result
);

/// <summary>
/// Evaluates the BSDF
/// </summary>
/// <param name="data">Material data</param>
/// <param name="normal">Normal at hit point</param>
/// <param name="outgoing">Vector towards camera or previous hit</param>
/// <param name="incoming">Sampled vector at this hit</param>
/// <returns>The value of the BSDF</returns>
glm::vec3 EvalBSDF(
	const BSDFMaterial& data,
	const glm::vec3& normal, const glm::vec3& outgoing, const glm::vec3& incoming
);

/// <summary>
/// Evaluates the BSDF's PDF
/// </summary>
/// <param name="data">Material data</param>
/// <param name="normal">Normal at hit point</param>
/// <param name="outgoing">Vector towards camera or previous hit</param>
/// <param name="incoming">Sampled vector at this hit</param>
/// <returns>The value of the PDF</returns>
float EvalPDF(
	const BSDFMaterial& data,
	const glm::vec3& normal, const glm::vec3& outgoing, const glm::vec3& incoming
);

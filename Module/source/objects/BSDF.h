#pragma once

#include "glm/glm.hpp"

#include "Sampler.h"

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

struct BSDFMaterial
{
	static int id;

	glm::vec3 baseColour{ 1.f, 1.f, 1.f };
	float ior = 1;

	bool roughnessOverridden = false;
	float roughness = 1.f;
	bool metallicOverridden = false;
	float metallic = 0.f;

	glm::vec3 diffuse{ 1.f, 1.f, 1.f };
	glm::vec3 specular{ 1.f, 1.f, 1.f };
	glm::vec3 transmission{ 1.f, 1.f, 1.f };

	bool etaOverridden = false;
	float eta = 1;
	float diffuseTransmission = 0.f;
	float specularTransmission = 0.f;

	bool thin = true;

	glm::uint activeLobes = static_cast<glm::uint>(LobeType::All);

	void PrepShadingData(
		const glm::vec3& hitColour, float hitMetalness, float hitRoughness,
		bool frontFacing
	);
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
	const BSDFMaterial& data, Sampler* sg, BSDFSample& result,
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
	const BSDFMaterial& data,
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
	const BSDFMaterial& data,
	const glm::vec3& normal, const glm::vec3& tangent, const glm::vec3& binormal,
	const glm::vec3& toEye, const glm::vec3& sampledDir
);

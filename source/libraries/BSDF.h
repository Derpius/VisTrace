#pragma once

#include "glm/glm.hpp"

#include "vistrace/ISampler.h"

constexpr float kMinGGXAlpha = 0.0064f;

// Most significant bit of each nibble reserved 
enum class LobeType : uint8_t
{
	None = 0,

	DiffuseReflection         = 0b00000001,
	DiffuseTransmission       = 0b00000010,
	SpecularReflection        = 0b00000100,
	SpecularTransmission      = 0b00001000,
	ConductiveReflection      = 0b00010000,
	DeltaSpecularReflection   = 0b00100000,
	DeltaSpecularTransmission = 0b01000000,
	DeltaConductiveReflection = 0b10000000,

	Reflection   = 0b10110101,
	Transmission = 0b01001010,

	Delta        = 0b11100000,
	NonDelta     = 0b00011111,

	Diffuse      = 0b00000011,
	Specular     = 0b11111100,
	Dielectric   = 0b01101111,
	Conductive   = 0b10010000,

	All = 0b11111111
};

inline LobeType operator |(const LobeType& lhs, const LobeType& rhs)
{
	return static_cast<LobeType>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

inline LobeType& operator |=(LobeType& lhs, const LobeType& rhs)
{
	lhs = lhs | rhs;
	return lhs;
}

inline LobeType operator &(const LobeType& lhs, const LobeType& rhs)
{
	return static_cast<LobeType>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
}

inline LobeType& operator &=(LobeType& lhs, const LobeType& rhs)
{
	lhs = lhs & rhs;
	return lhs;
}

struct BSDFMaterial
{
	static int id;

	glm::vec3 dielectricInput{ 1.f, 1.f, 1.f };
	glm::vec3 conductorInput{ 1.f, 1.f, 1.f };

	glm::vec3 dielectric{ 1.f, 1.f, 1.f };
	glm::vec3 conductor{ 1.f, 1.f, 1.f };

	glm::vec3 edgetint{ 1.f, 1.f, 1.f };
	float     falloff = 0.2;

	float ior = 1.5f;
	float outsideIoR = 1.f;

	bool roughnessOverridden = false;
	float roughness = 1.f;
	float linearRoughness = 1.f;

	bool metallicOverridden = false;
	float metallic = 0.f;

	bool anisotropyOverriden = false;
	float anisotropy = 0.f;
	bool anisotropicRotationOverriden = false;
	float anisotropicRotation = 0.f;

	float diffuseTransmission = 0.f;
	float specularTransmission = 0.f;

	bool thin = false;

	LobeType activeLobes = LobeType::All;

	void PrepShadingData(const glm::vec3& hitColour, float hitMetalness, float hitRoughness);
};

struct BSDFSample
{
	glm::vec3 scattered = glm::vec3(0.f);
	float pdf = 0.f;
	glm::vec3 weight = glm::vec3(0.f);
	LobeType lobe = LobeType::None;
};

bool SampleBSDF(
	const BSDFMaterial& data, VisTrace::ISampler* sg,
	const glm::vec3& normal, const glm::vec3& tangent, const glm::vec3& binormal,
	const glm::vec3& incidentWorld,
	BSDFSample& result
);

glm::vec3 EvalBSDF(
	const BSDFMaterial& data,
	const glm::vec3& normal, const glm::vec3& tangent, const glm::vec3& binormal,
	const glm::vec3& incidentWorld, const glm::vec3& scatteredWorld
);

float EvalPDF(
	const BSDFMaterial& data,
	const glm::vec3& normal, const glm::vec3& tangent, const glm::vec3& binormal,
	const glm::vec3& incidentWorld, const glm::vec3& scatteredWorld
);

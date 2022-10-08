#pragma once

#include "vistrace/IVTFTexture.h"

#include "BSPParser.h"

#include "glm/glm.hpp"
#include "glm/ext/matrix_transform.hpp"

#include <string>

enum class DetailBlendMode : uint8_t
{
	DecalModulate,
	Additive,
	TranslucentDetail,
	BlendFactorFade,
	TranslucentBase,
	UnlitAdditive,
	UnlitAdditiveThresholdFade,
	TwoPatternDecalModulate,
	Multiply,
	BaseMaskDetailAlpha,
	SSBump,
	SSBumpAlbedo
};

/// <summary>
/// VMT flags
/// </summary>
enum class MaterialFlags : uint32_t
{
	NONE = 0,
	debug = 1,
	no_fullbright = 2,
	no_draw = 4,
	use_in_fillrate_mode = 8,
	vertexcolor = 16,
	vertexalpha = 32,
	selfillum = 64,
	additive = 128,
	alphatest = 256,
	multipass = 512,
	znearer = 1024,
	model = 2048,
	flat = 4096,
	nocull = 8192,
	nofog = 16384,
	ignorez = 32768,
	decal = 65536,
	envmapsphere = 131072,
	noalphamod = 262144,
	envmapcameraspace = 524288,
	basealphaenvmapmask = 1048576,
	translucent = 2097152,
	normalmapalphaenvmapmask = 4194304,
	softwareskin = 8388608,
	opaquetexture = 16777216,
	envmapmode = 33554432,
	nodecal = 67108864,
	halflambert = 134217728,
	wireframe = 268435456,
	allowalphatocoverage = 536870912
};
inline MaterialFlags operator|(const MaterialFlags a, const MaterialFlags b)
{
	return static_cast<MaterialFlags>(static_cast<const uint32_t>(a) | static_cast<const uint32_t>(b));
}
inline MaterialFlags operator&(const MaterialFlags a, const MaterialFlags b)
{
	return static_cast<MaterialFlags>(static_cast<const uint32_t>(a) & static_cast<const uint32_t>(b));
}

struct Material
{
	std::string path = "";

	glm::vec4 colour = glm::vec4(1.f);

	std::string baseTexPath = "";
	const VisTrace::IVTFTexture* baseTexture = nullptr;
	glm::mat2x4 baseTexMat = glm::identity<glm::mat2x4>();

	std::string normalMapPath = "";
	const VisTrace::IVTFTexture* normalMap = nullptr;
	glm::mat2x4 normalMapMat = glm::identity<glm::mat2x4>();

	const VisTrace::IVTFTexture* mrao = nullptr;
	//glm::mat2x4 mraoMat     = glm::identity<glm::mat2x4>(); MRAO texture lookups are driven by the base texture

	std::string baseTexPath2 = "";
	const VisTrace::IVTFTexture* baseTexture2 = nullptr;
	glm::mat2x4 baseTexMat2 = glm::identity<glm::mat2x4>();

	std::string normalMapPath2 = "";
	const VisTrace::IVTFTexture* normalMap2 = nullptr;
	glm::mat2x4 normalMapMat2 = glm::identity<glm::mat2x4>();

	const VisTrace::IVTFTexture* mrao2 = nullptr;
	//glm::mat2x4 mraoMat2    = glm::identity<glm::mat2x4>(); MRAO texture lookups are driven by the base texture

	std::string blendTexPath = "";
	const VisTrace::IVTFTexture* blendTexture = nullptr;
	glm::mat2x4 blendTexMat = glm::identity<glm::mat2x4>();
	bool maskedBlending = false;

	std::string detailPath = "";
	const VisTrace::IVTFTexture* detail = nullptr;

	glm::mat2x4     detailMat = glm::identity<glm::mat2x4>();
	float           detailScale = 4.f;
	float           detailBlendFactor = 1.f;
	DetailBlendMode detailBlendMode = DetailBlendMode::DecalModulate;
	glm::vec3       detailTint = glm::vec3(1.f);
	bool            detailAlphaMaskBaseTexture = false;

	float texScale = 1.f;

	MaterialFlags flags = MaterialFlags::NONE;
	BSPEnums::SURF surfFlags = BSPEnums::SURF::NONE;

	float alphatestreference = 0.5f;

	bool water = false;
};

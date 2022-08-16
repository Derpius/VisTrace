#include "vistrace.h"

#include "Utils.h"

#include "VTFParser.h"
#include "BSPParser.h"
#include "GMFS.h"

#include "Sampler.h"
#include "RenderTarget.h"

#include "TraceResult.h"
#include "AccelStruct.h"

#include "BSDF.h"
#include "HDRI.h"
#include "Tonemapper.h"

using namespace GarrysMod::Lua;
using namespace VisTrace;

#pragma region Sampler
LUA_FUNCTION(CreateSampler)
{
	uint32_t seed = time(NULL);
	if (LUA->IsType(1, Type::Number)) seed = LUA->GetNumber(1);

	Sampler* pSampler = new Sampler(seed);
	LUA->PushUserType_Value(pSampler, Sampler::id);
	return 1;
}

LUA_FUNCTION(Sampler_gc)
{
	LUA->CheckType(1, Sampler::id);
	Sampler* pSampler = *LUA->GetUserType<Sampler*>(1, Sampler::id);

	LUA->SetUserType(1, NULL);
	delete pSampler;

	return 0;
}

LUA_FUNCTION(Sampler_GetFloat)
{
	LUA->CheckType(1, Sampler::id);
	Sampler* pSampler = *LUA->GetUserType<Sampler*>(1, Sampler::id);
	LUA->PushNumber(pSampler->GetFloat());
	return 1;
}

LUA_FUNCTION(Sampler_GetFloat2D)
{
	LUA->CheckType(1, Sampler::id);
	Sampler* pSampler = *LUA->GetUserType<Sampler*>(1, Sampler::id);

	float x, y;
	pSampler->GetFloat2D(x, y);
	LUA->PushNumber(x);
	LUA->PushNumber(y);
	return 2;
}

LUA_FUNCTION(Sampler_tostring)
{
	LUA->PushString("Sampler");
	return 1;
}
#pragma endregion

#pragma region Render Targets
/*
	uint8_t            width
	uint8_t            height
	RenderTargetFormat format
*/
LUA_FUNCTION(CreateRenderTarget)
{
	uint16_t width = LUA->CheckNumber(1), height = LUA->CheckNumber(2);
	RTFormat format = static_cast<RTFormat>(LUA->CheckNumber(3));

	switch (format) {
	case RTFormat::R8:
	case RTFormat::RG88:
	case RTFormat::RGB888:
	case RTFormat::RF:
	case RTFormat::RGFF:
	case RTFormat::RGBFFF:
		LUA->PushUserType_Value(new RenderTarget(width, height, format), RenderTarget::id);
		return 1;
	default:
		LUA->ArgError(3, "Invalid format");
		return 0; // prevents compiler warning
	}
}
LUA_FUNCTION(RT_gc)
{
	LUA->CheckType(1, RenderTarget::id);
	RenderTarget* pRt = *LUA->GetUserType<RenderTarget*>(1, RenderTarget::id);

	LUA->SetUserType(1, NULL);
	delete pRt;

	return 0;
}

LUA_FUNCTION(RT_IsValid)
{
	RenderTarget** ppRt = LUA->GetUserType<RenderTarget*>(1, RenderTarget::id);
	LUA->PushBool(ppRt != nullptr && (*ppRt)->IsValid());
	return 1;
}

LUA_FUNCTION(RT_Resize)
{
	LUA->CheckType(1, RenderTarget::id);
	RenderTarget* pRt = *LUA->GetUserType<RenderTarget*>(1, RenderTarget::id);
	LUA->PushBool(pRt->Resize(LUA->CheckNumber(2), LUA->CheckNumber(3)));
	return 1;
}

LUA_FUNCTION(RT_GetWidth)
{
	LUA->CheckType(1, RenderTarget::id);
	RenderTarget* pRt = *LUA->GetUserType<RenderTarget*>(1, RenderTarget::id);
	LUA->PushNumber(pRt->GetWidth());
	return 1;
}
LUA_FUNCTION(RT_GetHeight)
{
	LUA->CheckType(1, RenderTarget::id);
	RenderTarget* pRt = *LUA->GetUserType<RenderTarget*>(1, RenderTarget::id);
	LUA->PushNumber(pRt->GetHeight());
	return 1;
}
LUA_FUNCTION(RT_GetFormat)
{
	LUA->CheckType(1, RenderTarget::id);
	RenderTarget* pRt = *LUA->GetUserType<RenderTarget*>(1, RenderTarget::id);
	LUA->PushNumber(static_cast<double>(pRt->GetFormat()));
	return 1;
}

LUA_FUNCTION(RT_GetPixel)
{
	LUA->CheckType(1, RenderTarget::id);
	RenderTarget* pRt = *LUA->GetUserType<RenderTarget*>(1, RenderTarget::id);
	if (!pRt->IsValid()) LUA->ThrowError("Invalid render target");

	uint16_t x = LUA->CheckNumber(2), y = LUA->CheckNumber(3);
	if (x >= pRt->GetWidth() || y >= pRt->GetHeight()) LUA->ThrowError("Pixel coordinate out of range");
	Pixel pixel = pRt->GetPixel(x, y);

	for (int channel = 0; channel < CHANNELS[static_cast<uint8_t>(pRt->GetFormat())]; channel++) {
		LUA->PushNumber(pixel[channel]);
	}
	return CHANNELS[static_cast<uint8_t>(pRt->GetFormat())];
}
LUA_FUNCTION(RT_SetPixel)
{
	LUA->CheckType(1, RenderTarget::id);
	RenderTarget* pRt = *LUA->GetUserType<RenderTarget*>(1, RenderTarget::id);
	if (!pRt->IsValid()) LUA->ThrowError("Invalid render target");

	uint16_t x = LUA->CheckNumber(2), y = LUA->CheckNumber(3);
	if (x >= pRt->GetWidth() || y >= pRt->GetHeight()) LUA->ThrowError("Pixel coordinate out of range");

	Pixel pixel{};
	for (int channel = 0; channel < CHANNELS[static_cast<uint8_t>(pRt->GetFormat())]; channel++) {
		pixel[channel] = LUA->GetNumber(4 + channel);
	}
	pRt->SetPixel(x, y, pixel);

	return 0;
}

LUA_FUNCTION(RT_Tonemap)
{
	LUA->CheckType(1, RenderTarget::id);
	RenderTarget* pRt = *LUA->GetUserType<RenderTarget*>(1, RenderTarget::id);
	if (!pRt->IsValid()) LUA->ThrowError("Invalid render target");
	if (pRt->GetFormat() != RTFormat::RGBFFF) LUA->ThrowError("Render target's format must be RGBFFF");

	Tonemap(pRt);
	return 0;
}

LUA_FUNCTION(RT_tostring)
{
	LUA->PushString("VisTraceRT");
	return 1;
}
#pragma endregion

#pragma region TraceResult
LUA_FUNCTION(TraceResult_Pos)
{
	LUA->CheckType(1, TraceResult::id);
	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);

	LUA->PushVector(MakeVector(pResult->GetPos().x, pResult->GetPos().y, pResult->GetPos().z));
	return 1;
}
LUA_FUNCTION(TraceResult_Distance)
{
	LUA->CheckType(1, TraceResult::id);
	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);
	LUA->PushNumber(pResult->distance);
	return 1;
}

LUA_FUNCTION(TraceResult_Entity)
{
	LUA->CheckType(1, TraceResult::id);
	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);

	LUA->PushSpecial(SPECIAL_GLOB);
	LUA->GetField(-1, "Entity");
	LUA->PushNumber(pResult->entIdx);
	LUA->Call(1, 1);

	CBaseEntity* pEnt = LUA->GetUserType<CBaseEntity>(-1, Type::Entity);
	if (pEnt == nullptr || pEnt != pResult->rawEnt) {
		LUA->GetField(-2, "Entity");
		LUA->PushNumber(-1);
		LUA->Call(1, 1);
	}

	return 1;
}

LUA_FUNCTION(TraceResult_GeometricNormal)
{
	LUA->CheckType(1, TraceResult::id);
	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);

	LUA->PushVector(MakeVector(pResult->geometricNormal.x, pResult->geometricNormal.y, pResult->geometricNormal.z));
	return 1;
}

LUA_FUNCTION(TraceResult_Normal)
{
	LUA->CheckType(1, TraceResult::id);
	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);

	const glm::vec3& v = pResult->GetNormal();
	LUA->PushVector(MakeVector(v.x, v.y, v.z));
	return 1;
}
LUA_FUNCTION(TraceResult_Tangent)
{
	LUA->CheckType(1, TraceResult::id);
	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);

	const glm::vec3& v = pResult->GetTangent();
	LUA->PushVector(MakeVector(v.x, v.y, v.z));
	return 1;
}
LUA_FUNCTION(TraceResult_Binormal)
{
	LUA->CheckType(1, TraceResult::id);
	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);

	const glm::vec3& v = pResult->GetBinormal();
	LUA->PushVector(MakeVector(v.x, v.y, v.z));
	return 1;
}

LUA_FUNCTION(TraceResult_Barycentric)
{
	LUA->CheckType(1, TraceResult::id);
	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);

	LUA->PushVector(MakeVector(pResult->uvw.x, pResult->uvw.y, pResult->uvw.z));
	return 1;
}
LUA_FUNCTION(TraceResult_TextureUV)
{
	LUA->CheckType(1, TraceResult::id);
	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);

	LUA->CreateTable();
	LUA->PushNumber(pResult->texUV.x);
	LUA->SetField(-2, "u");
	LUA->PushNumber(pResult->texUV.y);
	LUA->SetField(-2, "v");
	return 1;
}

LUA_FUNCTION(TraceResult_SubMaterialIndex)
{
	LUA->CheckType(1, TraceResult::id);
	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);

	LUA->PushNumber(pResult->submatIdx + 1);
	return 1;
}

LUA_FUNCTION(TraceResult_Albedo)
{
	LUA->CheckType(1, TraceResult::id);
	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);

	const glm::vec3& v = pResult->GetAlbedo();
	LUA->PushVector(MakeVector(v.x, v.y, v.z));
	return 1;
}
LUA_FUNCTION(TraceResult_Alpha)
{
	LUA->CheckType(1, TraceResult::id);
	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);
	LUA->PushNumber(pResult->GetAlpha());
	return 1;
}
LUA_FUNCTION(TraceResult_Metalness)
{
	LUA->CheckType(1, TraceResult::id);
	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);
	LUA->PushNumber(pResult->GetMetalness());
	return 1;
}
LUA_FUNCTION(TraceResult_Roughness)
{
	LUA->CheckType(1, TraceResult::id);
	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);
	LUA->PushNumber(pResult->GetRoughness());
	return 1;
}

LUA_FUNCTION(TraceResult_MaterialFlags)
{
	LUA->CheckType(1, TraceResult::id);
	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);
	LUA->PushNumber(static_cast<double>(pResult->materialFlags));
	return 1;
}
LUA_FUNCTION(TraceResult_SurfaceFlags)
{
	LUA->CheckType(1, TraceResult::id);
	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);
	LUA->PushNumber(static_cast<double>(pResult->surfaceFlags));
	return 1;
}

LUA_FUNCTION(TraceResult_HitSky)
{
	LUA->CheckType(1, TraceResult::id);
	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);
	LUA->PushBool(pResult->hitSky);
	return 1;
}
LUA_FUNCTION(TraceResult_HitWater)
{
	LUA->CheckType(1, TraceResult::id);
	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);
	LUA->PushBool(pResult->hitWater);
	return 1;
}

LUA_FUNCTION(TraceResult_FrontFacing)
{
	LUA->CheckType(1, TraceResult::id);
	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);
	LUA->PushBool(pResult->frontFacing);
	return 1;
}

LUA_FUNCTION(TraceResult_BaseMIPLevel)
{
	LUA->CheckType(1, TraceResult::id);
	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);
	LUA->PushNumber(pResult->GetBaseMIPLevel());
	return 1;
}

LUA_FUNCTION(TraceResult_tostring)
{
	LUA->PushString("VisTraceResult");
	return 1;
}
#pragma endregion

#pragma region Tracing API
static int AccelStruct_id;
static World* g_pWorld = nullptr;

LUA_FUNCTION(AccelStruct_gc)
{
	LUA->CheckType(1, AccelStruct_id);
	AccelStruct* pAccelStruct = *LUA->GetUserType<AccelStruct*>(1, AccelStruct_id);

	LUA->SetUserType(1, NULL);
	delete pAccelStruct;

	return 0;
}

/*
	table[Entity] entities = {}
	boolean       traceWorld = true

	returns AccelStruct
*/
LUA_FUNCTION(CreateAccel)
{
	bool traceWorld = true;
	if (LUA->IsType(2, Type::Bool)) traceWorld = LUA->GetBool(2);

	AccelStruct* pAccelStruct = new AccelStruct();

	if (LUA->Top() == 0) LUA->CreateTable();
	else if (LUA->IsType(1, Type::Nil)) {
		LUA->Pop(LUA->Top());
		LUA->CreateTable();
	} else {
		if (!LUA->IsType(1, Type::Table)) {
			delete pAccelStruct; // Throwing an error wont destruct
			LUA->CheckType(1, Type::Table); // Still checktype so we get the formatted error for free
		}
		LUA->Pop(LUA->Top() - 1); // Pop all but the table
	}
	pAccelStruct->PopulateAccel(LUA, traceWorld ? g_pWorld : nullptr);

	LUA->PushUserType_Value(pAccelStruct, AccelStruct_id);
	return 1;
}

/*
	AccelStruct   accel
	table[Entity] entities = {}
*/
LUA_FUNCTION(RebuildAccel)
{
	LUA->CheckType(1, AccelStruct_id);

	bool traceWorld = true;
	if (LUA->IsType(3, Type::Bool)) traceWorld = LUA->GetBool(3);

	AccelStruct* pAccelStruct = *LUA->GetUserType<AccelStruct*>(1, AccelStruct_id);

	if (LUA->Top() == 1) LUA->CreateTable();
	else if (LUA->IsType(2, Type::Nil)) {
		LUA->Pop(LUA->Top());
		LUA->CreateTable();
	} else {
		LUA->CheckType(2, Type::Table);
		LUA->Pop(LUA->Top() - 2); // Pop all but the table (and self)
	}
	pAccelStruct->PopulateAccel(LUA, traceWorld ? g_pWorld : nullptr);

	return 0;
}

/*
	AccelStruct accel
	Vector      origin
	Vector      direction
	float       tMin = 0
	float       tMax = FLT_MAX
	float       coneWidth = -1
	float       coneAngle = -1

	returns TraceResult
*/
LUA_FUNCTION(TraverseScene)
{
	LUA->CheckType(1, AccelStruct_id);
	AccelStruct* pAccelStruct = *LUA->GetUserType<AccelStruct*>(1, AccelStruct_id);
	return pAccelStruct->Traverse(LUA);
}

LUA_FUNCTION(AccelStruct_tostring)
{
	LUA->PushString("AccelStruct");
	return 1;
}
#pragma endregion

#pragma region BSDFMaterial
LUA_FUNCTION(CreateMaterial)
{
	LUA->PushUserType_Value(BSDFMaterial{}, BSDFMaterial::id);
	return 1;
}

LUA_FUNCTION(Material_Colour)
{
	LUA->CheckType(1, BSDFMaterial::id);
	LUA->CheckType(2, Type::Vector);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(1, BSDFMaterial::id);
	Vector v = LUA->GetVector(2);
	pMat->baseColour = glm::vec3(v.x, v.y, v.z);
	return 0;
}

LUA_FUNCTION(Material_Metalness)
{
	LUA->CheckType(1, BSDFMaterial::id);
	LUA->CheckType(2, Type::Number);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(1, BSDFMaterial::id);
	pMat->metallic = LUA->GetNumber(2);
	pMat->metallicOverridden = true;
	return 0;
}
LUA_FUNCTION(Material_Roughness)
{
	LUA->CheckType(1, BSDFMaterial::id);
	LUA->CheckType(2, Type::Number);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(1, BSDFMaterial::id);
	pMat->roughness = LUA->GetNumber(2);
	pMat->roughnessOverridden = true;
	return 0;
}

LUA_FUNCTION(Material_IoR)
{
	LUA->CheckType(1, BSDFMaterial::id);
	LUA->CheckType(2, Type::Number);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(1, BSDFMaterial::id);
	pMat->ior = LUA->GetNumber(2);
	return 0;
}
LUA_FUNCTION(Material_RelativeIoR)
{
	LUA->CheckType(1, BSDFMaterial::id);
	LUA->CheckType(2, Type::Number);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(1, BSDFMaterial::id);
	pMat->eta = LUA->GetNumber(2);
	pMat->etaOverridden = true;
	return 0;
}

LUA_FUNCTION(Material_DiffuseTransmission)
{
	LUA->CheckType(1, BSDFMaterial::id);
	LUA->CheckType(2, Type::Number);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(1, BSDFMaterial::id);
	pMat->diffuseTransmission = LUA->GetNumber(2);
	return 0;
}
LUA_FUNCTION(Material_SpecularTransmission)
{
	LUA->CheckType(1, BSDFMaterial::id);
	LUA->CheckType(2, Type::Number);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(1, BSDFMaterial::id);
	pMat->specularTransmission = LUA->GetNumber(2);
	return 0;
}

LUA_FUNCTION(Material_Thin)
{
	LUA->CheckType(1, BSDFMaterial::id);
	LUA->CheckType(2, Type::Bool);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(1, BSDFMaterial::id);
	pMat->thin = LUA->GetBool(2);
	return 0;
}

LUA_FUNCTION(Material_tostring)
{
	LUA->PushString("BSDFMaterial");
	return 1;
}
#pragma endregion

#pragma region BxDF API
/*
	TraceResult  self
	Sampler      sampler
	BSDFMaterial material

	returns:
	bool valid
	BSDFSample? sample
*/
LUA_FUNCTION(TraceResult_SampleBSDF)
{
	LUA->CheckType(1, TraceResult::id);
	LUA->CheckType(2, Sampler::id);
	LUA->CheckType(3, BSDFMaterial::id);

	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);
	Sampler* pSampler = *LUA->GetUserType<Sampler*>(2, Sampler::id);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(3, BSDFMaterial::id);
	pMat->PrepShadingData(
		pResult->GetAlbedo(),
		pResult->GetMetalness(), pResult->GetRoughness(),
		pResult->frontFacing
	);

	LUA->Pop(LUA->Top());

	BSDFSample sample;
	bool valid = SampleFalcorBSDF(
		*pMat, pSampler, sample,
		pResult->GetNormal(), pResult->GetTangent(), pResult->GetBinormal(),
		pResult->wo
	);

	if (!valid) {
		LUA->PushBool(false);
		return 1;
	}

	LUA->PushBool(true);

	LUA->CreateTable();
	LUA->PushVector(MakeVector(sample.wo.x, sample.wo.y, sample.wo.z));
	LUA->SetField(-2, "wo");
	LUA->PushVector(MakeVector(sample.weight.x, sample.weight.y, sample.weight.z));
	LUA->SetField(-2, "weight");

	LUA->PushNumber(sample.pdf);
	LUA->SetField(-2, "pdf");

	LUA->PushNumber(sample.lobe);
	LUA->SetField(-2, "lobe");

	return 2;
}

/*
	TraceResult  self
	BSDFMaterial material
	Vector       wi

	returns:
	Vector colour
*/
LUA_FUNCTION(TraceResult_EvalBSDF)
{
	LUA->CheckType(1, TraceResult::id);
	LUA->CheckType(2, BSDFMaterial::id);
	LUA->CheckType(3, Type::Vector);

	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);

	glm::vec3 incoming;
	{
		Vector v = LUA->GetVector(3);
		incoming = glm::vec3(v.x, v.y, v.z);
	}

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(2, BSDFMaterial::id);
	pMat->PrepShadingData(
		pResult->GetAlbedo(),
		pResult->GetMetalness(), pResult->GetRoughness(),
		pResult->frontFacing
	);

	LUA->Pop(LUA->Top());

	glm::vec3 colour = EvalFalcorBSDF(
		*pMat,
		pResult->GetNormal(), pResult->GetTangent(), pResult->GetBinormal(),
		pResult->wo, incoming
	);

	LUA->PushVector(MakeVector(colour.x, colour.y, colour.z));
	return 1;
}

/*
	TraceResult  self
	BSDFMaterial material
	Vector       wi

	returns:
	float pdf
*/
LUA_FUNCTION(TraceResult_EvalPDF)
{
	LUA->CheckType(1, TraceResult::id);
	LUA->CheckType(2, BSDFMaterial::id);
	LUA->CheckType(3, Type::Vector);

	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);

	glm::vec3 incoming;
	{
		Vector v = LUA->GetVector(3);
		incoming = glm::vec3(v.x, v.y, v.z);
	}

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(2, BSDFMaterial::id);
	pMat->PrepShadingData(
		pResult->GetAlbedo(),
		pResult->GetMetalness(), pResult->GetRoughness(),
		pResult->frontFacing
	);

	LUA->Pop(LUA->Top());

	float pdf = EvalPDFFalcorBSDF(
		*pMat,
		pResult->GetNormal(), pResult->GetTangent(), pResult->GetBinormal(),
		pResult->wo, incoming
	);

	LUA->PushNumber(pdf);
	return 1;
}
#pragma endregion

#pragma region HDRI API
LUA_FUNCTION(LoadHDRI)
{
	LUA->CheckString(1);

	uint16_t importanceDim = 512;
	if (LUA->IsType(2, Type::Number) && LUA->GetNumber(2) > 0) {
		importanceDim = LUA->GetNumber(2);
	}

	uint16_t importanceSamples = 64;
	if (LUA->IsType(3, Type::Number) && LUA->GetNumber(3) > 0) {
		importanceSamples = LUA->GetNumber(3);
	}

	std::string texturePath = "vistrace_hdris/" + std::string(LUA->GetString(1)) + ".hdr";
	if (!FileSystem::Exists(texturePath.c_str(), "DATA"))
		LUA->ThrowError("HDRI file does not exist (place HDRIs in .hdr format inside data/vistrace_hdris/)");
	FileHandle_t file = FileSystem::Open(texturePath.c_str(), "rb", "DATA");

	uint32_t filesize = FileSystem::Size(file);
	uint8_t* data = reinterpret_cast<uint8_t*>(malloc(filesize));
	if (data == nullptr) LUA->ThrowError("Failed to allocate memory for HDRI");

	FileSystem::Read(data, filesize, file);
	FileSystem::Close(file);

	HDRI* pHDRI = new HDRI(data, filesize, importanceDim, importanceSamples);
	LUA->PushUserType_Value(pHDRI, HDRI::id);
	free(data);

	return 1;
}

LUA_FUNCTION(HDRI_IsValid)
{
	LUA->CheckType(1, HDRI::id);
	HDRI* pHDRI = *LUA->GetUserType<HDRI*>(1, HDRI::id);
	LUA->PushBool(pHDRI->IsValid());
	return 1;
}

LUA_FUNCTION(HDRI_GetPixel)
{
	LUA->CheckType(1, HDRI::id);
	LUA->CheckType(2, Type::Vector);

	HDRI* pHDRI = *LUA->GetUserType<HDRI*>(1, HDRI::id);
	Vector direction = LUA->GetVector(2);

	glm::vec3 colour = pHDRI->GetPixel(glm::vec3(direction.x, direction.y, direction.z));
	LUA->PushVector(MakeVector(colour.r, colour.g, colour.b));
	return 1;
}

LUA_FUNCTION(HDRI_EvalPDF)
{
	LUA->CheckType(1, HDRI::id);
	LUA->CheckType(2, Type::Vector);

	HDRI* pHDRI = *LUA->GetUserType<HDRI*>(1, HDRI::id);
	Vector direction = LUA->GetVector(2);

	float pdf = pHDRI->EvalPDF(glm::vec3(direction.x, direction.y, direction.z));
	LUA->PushNumber(pdf);
	return 1;
}

LUA_FUNCTION(HDRI_Sample)
{
	LUA->CheckType(1, HDRI::id);
	LUA->CheckType(2, Sampler::id);

	HDRI* pHDRI = *LUA->GetUserType<HDRI*>(1, HDRI::id);
	Sampler* pSampler = *LUA->GetUserType<Sampler*>(2, Sampler::id);

	float pdf = 0.f;
	glm::vec3 sampleDir{ 0.f };
	glm::vec3 colour{ 0.f };

	bool valid = pHDRI->Sample(pdf, sampleDir, colour, pSampler);

	if (!valid) {
		LUA->PushBool(false);
		return 1;
	}

	LUA->PushBool(true);
	LUA->PushVector(MakeVector(sampleDir.x, sampleDir.y, sampleDir.z));
	LUA->PushVector(MakeVector(colour.r, colour.g, colour.b));
	LUA->PushNumber(pdf);
	return 4;
}

LUA_FUNCTION(HDRI_SetAngles)
{
	LUA->CheckType(1, HDRI::id);
	LUA->CheckType(2, Type::Angle);

	HDRI* pHDRI = *LUA->GetUserType<HDRI*>(1, HDRI::id);
	QAngle ang = LUA->GetAngle(2);

	pHDRI->SetAngle(glm::vec3(ang.x, ang.y, ang.z));

	return 0;
}

LUA_FUNCTION(HDRI_tostring)
{
	LUA->PushString("HDRI");
	return 1;
}

LUA_FUNCTION(HDRI_gc)
{
	LUA->CheckType(1, HDRI::id);
	HDRI* pHDRI = *LUA->GetUserType<HDRI*>(1, HDRI::id);
	delete pHDRI;
	return 0;
}
#pragma endregion

#pragma region Helpers
LUA_FUNCTION(CalcRayOrigin)
{
	assert(sizeof(int) == sizeof(float));
	using namespace glm;

	LUA->CheckType(1, Type::Vector);
	LUA->CheckType(2, Type::Vector);

	vec3 pos, normal;
	{
		Vector v = LUA->GetVector(1);
		pos = vec3(v.x, v.y, v.z);

		v = LUA->GetVector(2);
		normal = vec3(v.x, v.y, v.z);
	}

	const float origin = 1.f / 32.f;
	const float fScale = 1.f / 65536.f;
	const float iScale = 256.f;

	// Per-component integer offset to bit representation of fp32 position.
	ivec3 iOff = ivec3(normal * iScale);
	vec3 iPos = intBitsToFloat(
		floatBitsToInt(pos) +
		ivec3(
			pos.x < 0.f ? -iOff.x : iOff.x,
			pos.y < 0.f ? -iOff.y : iOff.y,
			pos.z < 0.f ? -iOff.z : iOff.z
		)
	);

	// Select per-component between small fixed offset or above variable offset depending on distance to origin.
	vec3 fOff = normal * fScale;

	LUA->PushVector(MakeVector(
		abs(pos.x) < origin ? pos.x + fOff.x : iPos.x,
		abs(pos.y) < origin ? pos.y + fOff.y : iPos.y,
		abs(pos.z) < origin ? pos.z + fOff.z : iPos.z
	));
	return 1;
}
#pragma endregion

LUA_FUNCTION(GM_Initialize)
{
	printLua(LUA, "VisTrace: Loading map...");
	LUA->PushSpecial(SPECIAL_GLOB);
	LUA->GetField(-1, "game");
	LUA->GetField(-1, "GetMap");
	LUA->Call(0, 1);
	const char* mapName = LUA->GetString();
	LUA->Pop(2); // _G and game

	g_pWorld = new World(LUA, mapName);
	if (!g_pWorld->IsValid()) {
		delete g_pWorld;
		g_pWorld = nullptr;
		LUA->ThrowError("Failed to load map, acceleration structures will only trace props");
	}

	printLua(LUA, "VisTrace: Map loaded successfully!");

	// Call init hook to tell extensions to load
	LUA->PushSpecial(SPECIAL_GLOB);
	LUA->GetField(-1, "hook");
	LUA->GetField(-1, "Call");
	LUA->PushString("VisTraceInit");
	LUA->Call(1, 0);
	LUA->Pop(2);

	return 0;
}

#define PUSH_C_FUNC(function) LUA->PushCFunction(function); LUA->SetField(-2, #function)

#define PUSH_ENUM(enum, field) \
LUA->PushNumber(static_cast<double>(enum::field)); \
LUA->SetField(-2, #field)

GMOD_MODULE_OPEN()
{
	switch (FileSystem::LoadFileSystem()) {
	case FILESYSTEM_STATUS::MODULELOAD_FAILED:
		LUA->ThrowError("Failed to get filesystem module handle");
	case FILESYSTEM_STATUS::GETPROCADDR_FAILED:
		LUA->ThrowError("Failed to get CreateInterface export");
	case FILESYSTEM_STATUS::CREATEINTERFACE_FAILED:
		LUA->ThrowError("CreateInterface failed");
	case FILESYSTEM_STATUS::OK:
		printLua(LUA, "VisTrace: Loaded filesystem interface successfully");
	}

	LUA->PushSpecial(SPECIAL_GLOB);
	LUA->GetField(-1, "hook");
	LUA->GetField(-1, "Add");
	LUA->PushString("Initialize");
	LUA->PushString("VisTrace.LoadWorld");
	LUA->PushCFunction(GM_Initialize);
	LUA->Call(3, 0);
	LUA->Pop(2); // _G and hook

	RenderTarget::id = LUA->CreateMetaTable("VisTraceRT");
	LUA->PushSpecial(SPECIAL_REG);
	LUA->PushNumber(RenderTarget::id);
	LUA->SetField(-2, "VisTraceRT_id");
	LUA->Pop(); // Pop the registry
		LUA->Push(-1);
		LUA->SetField(-2, "__index");
		LUA->PushCFunction(RT_tostring);
		LUA->SetField(-2, "__tostring");
		LUA->PushCFunction(RT_gc);
		LUA->SetField(-2, "__gc");

		LUA->PushCFunction(RT_IsValid);
		LUA->SetField(-2, "IsValid");

		LUA->PushCFunction(RT_Resize);
		LUA->SetField(-2, "Resize");

		LUA->PushCFunction(RT_GetWidth);
		LUA->SetField(-2, "GetWidth");
		LUA->PushCFunction(RT_GetHeight);
		LUA->SetField(-2, "GetHeight");
		LUA->PushCFunction(RT_GetFormat);
		LUA->SetField(-2, "GetFormat");

		LUA->PushCFunction(RT_GetPixel);
		LUA->SetField(-2, "GetPixel");
		LUA->PushCFunction(RT_SetPixel);
		LUA->SetField(-2, "SetPixel");

		LUA->PushCFunction(RT_Tonemap);
		LUA->SetField(-2, "Tonemap");
	LUA->Pop();

	TraceResult::id = LUA->CreateMetaTable("VisTraceResult");
		LUA->Push(-1);
		LUA->SetField(-2, "__index");
		LUA->PushCFunction(TraceResult_tostring);
		LUA->SetField(-2, "__tostring");

		LUA->PushCFunction(TraceResult_Pos);
		LUA->SetField(-2, "Pos");
		LUA->PushCFunction(TraceResult_Distance);
		LUA->SetField(-2, "Distance");

		LUA->PushCFunction(TraceResult_Entity);
		LUA->SetField(-2, "Entity");

		LUA->PushCFunction(TraceResult_GeometricNormal);
		LUA->SetField(-2, "GeometricNormal");
		LUA->PushCFunction(TraceResult_Normal);
		LUA->SetField(-2, "Normal");
		LUA->PushCFunction(TraceResult_Tangent);
		LUA->SetField(-2, "Tangent");
		LUA->PushCFunction(TraceResult_Binormal);
		LUA->SetField(-2, "Binormal");

		LUA->PushCFunction(TraceResult_Barycentric);
		LUA->SetField(-2, "Barycentric");
		LUA->PushCFunction(TraceResult_TextureUV);
		LUA->SetField(-2, "TextureUV");

		LUA->PushCFunction(TraceResult_SubMaterialIndex);
		LUA->SetField(-2, "SubMaterialIndex");

		LUA->PushCFunction(TraceResult_Albedo);
		LUA->SetField(-2, "Albedo");
		LUA->PushCFunction(TraceResult_Alpha);
		LUA->SetField(-2, "Alpha");
		LUA->PushCFunction(TraceResult_Metalness);
		LUA->SetField(-2, "Metalness");
		LUA->PushCFunction(TraceResult_Roughness);
		LUA->SetField(-2, "Roughness");

		LUA->PushCFunction(TraceResult_MaterialFlags);
		LUA->SetField(-2, "MaterialFlags");
		LUA->PushCFunction(TraceResult_SurfaceFlags);
		LUA->SetField(-2, "SurfaceFlags");

		LUA->PushCFunction(TraceResult_HitSky);
		LUA->SetField(-2, "HitSky");
		LUA->PushCFunction(TraceResult_HitWater);
		LUA->SetField(-2, "HitWater");

		LUA->PushCFunction(TraceResult_FrontFacing);
		LUA->SetField(-2, "FrontFacing");

		LUA->PushCFunction(TraceResult_BaseMIPLevel);
		LUA->SetField(-2, "BaseMIPLevel");

		LUA->PushCFunction(TraceResult_SampleBSDF);
		LUA->SetField(-2, "SampleBSDF");
		LUA->PushCFunction(TraceResult_EvalBSDF);
		LUA->SetField(-2, "EvalBSDF");
		LUA->PushCFunction(TraceResult_EvalPDF);
		LUA->SetField(-2, "EvalPDF");
	LUA->Pop();

	AccelStruct_id = LUA->CreateMetaTable("AccelStruct");
		LUA->Push(-1);
		LUA->SetField(-2, "__index");
		LUA->PushCFunction(AccelStruct_tostring);
		LUA->SetField(-2, "__tostring");
		LUA->PushCFunction(AccelStruct_gc);
		LUA->SetField(-2, "__gc");

		LUA->PushCFunction(RebuildAccel);
		LUA->SetField(-2, "Rebuild");
		LUA->PushCFunction(TraverseScene);
		LUA->SetField(-2, "Traverse");
	LUA->Pop();

	Sampler::id = LUA->CreateMetaTable("Sampler");
	LUA->PushSpecial(SPECIAL_REG);
	LUA->PushNumber(Sampler::id);
	LUA->SetField(-2, "Sampler::id");
	LUA->Pop(); // Pop the registry
		LUA->Push(-1);
		LUA->SetField(-2, "__index");
		LUA->PushCFunction(Sampler_tostring);
		LUA->SetField(-2, "__tostring");
		LUA->PushCFunction(Sampler_gc);
		LUA->SetField(-2, "__gc");

		LUA->PushCFunction(Sampler_GetFloat);
		LUA->SetField(-2, "GetFloat");
		LUA->PushCFunction(Sampler_GetFloat2D);
		LUA->SetField(-2, "GetFloat2D");
	LUA->Pop();

	HDRI::id = LUA->CreateMetaTable("HDRI");
		LUA->Push(-1);
		LUA->SetField(-2, "__index");
		LUA->PushCFunction(HDRI_tostring);
		LUA->SetField(-2, "__tostring");
		LUA->PushCFunction(HDRI_gc);
		LUA->SetField(-2, "__gc");

		LUA->PushCFunction(HDRI_IsValid);
		LUA->SetField(-2, "IsValid");

		LUA->PushCFunction(HDRI_GetPixel);
		LUA->SetField(-2, "GetPixel");

		LUA->PushCFunction(HDRI_EvalPDF);
		LUA->SetField(-2, "EvalPDF");
		LUA->PushCFunction(HDRI_Sample);
		LUA->SetField(-2, "Sample");

		LUA->PushCFunction(HDRI_SetAngles);
		LUA->SetField(-2, "SetAngles");
	LUA->Pop();

	BSDFMaterial::id = LUA->CreateMetaTable("BSDFMaterial");
		LUA->Push(-1);
		LUA->SetField(-2, "__index");
		LUA->PushCFunction(Material_tostring);
		LUA->SetField(-2, "__tostring");

		LUA->PushCFunction(Material_Colour);
		LUA->SetField(-2, "Colour");

		LUA->PushCFunction(Material_Metalness);
		LUA->SetField(-2, "Metalness");
		LUA->PushCFunction(Material_Roughness);
		LUA->SetField(-2, "Roughness");

		LUA->PushCFunction(Material_IoR);
		LUA->SetField(-2, "IoR");
		LUA->PushCFunction(Material_RelativeIoR);
		LUA->SetField(-2, "RelativeIoR");

		LUA->PushCFunction(Material_DiffuseTransmission);
		LUA->SetField(-2, "DiffuseTransmission");
		LUA->PushCFunction(Material_SpecularTransmission);
		LUA->SetField(-2, "SpecularTransmission");

		LUA->PushCFunction(Material_Thin);
		LUA->SetField(-2, "Thin");
	LUA->Pop();

	LUA->PushSpecial(SPECIAL_GLOB);
		LUA->CreateTable();
			PUSH_C_FUNC(CreateRenderTarget);
			PUSH_C_FUNC(CreateAccel);
			PUSH_C_FUNC(CreateSampler);
			PUSH_C_FUNC(CreateMaterial);
			PUSH_C_FUNC(LoadHDRI);

			PUSH_C_FUNC(CalcRayOrigin);
		LUA->SetField(-2, "vistrace");

		LUA->CreateTable();
			PUSH_ENUM(RTFormat, R8);
			PUSH_ENUM(RTFormat, RG88);
			PUSH_ENUM(RTFormat, RGB888);
			PUSH_ENUM(RTFormat, RF);
			PUSH_ENUM(RTFormat, RGFF);
			PUSH_ENUM(RTFormat, RGBFFF);

			PUSH_ENUM(RTFormat, Size);

			PUSH_ENUM(RTFormat, Albedo);
			PUSH_ENUM(RTFormat, Normal);
		LUA->SetField(-2, "VisTraceRTFormat");
	LUA->Pop();

	printLua(LUA, "VisTrace Loaded!");
	return 0;
}

GMOD_MODULE_CLOSE()
{
	if (g_pWorld != nullptr) delete g_pWorld;
	return 0;
}
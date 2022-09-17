#include "vistrace.h"

#include "Utils.h"

#include "BSPParser.h"
#include "GMFS.h"

#include "Sampler.h"
#include "RenderTarget.h"
#include "VTFTexture.h"
#include "SourceTypes.h"

#include "TraceResult.h"
#include "AccelStruct.h"

#include "BSDF.h"
#include "HDRI.h"
#include "Tonemapper.h"

using namespace GarrysMod::Lua;
using namespace VisTrace;

#pragma region Sampler
LUA_FUNCTION(vistrace_CreateSampler)
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
	ISampler* pSampler = *LUA->GetUserType<ISampler*>(1, Sampler::id);

	LUA->SetUserType(1, NULL);
	delete pSampler;

	return 0;
}

LUA_FUNCTION(Sampler_GetFloat)
{
	LUA->CheckType(1, Sampler::id);
	ISampler* pSampler = *LUA->GetUserType<ISampler*>(1, Sampler::id);
	LUA->PushNumber(pSampler->GetFloat());
	return 1;
}

LUA_FUNCTION(Sampler_GetFloat2D)
{
	LUA->CheckType(1, Sampler::id);
	ISampler* pSampler = *LUA->GetUserType<ISampler*>(1, Sampler::id);

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

#pragma region VTF Textures
/*
	string path
*/
LUA_FUNCTION(vistrace_LoadTexture)
{
	const char* path = LUA->CheckString(1);

	IVTFTexture* pTex = new VTFTextureWrapper(path);
	if (!pTex->IsValid()) {
		delete pTex;
		LUA->ThrowError("Failed to load texture");
	}

	LUA->PushUserType_Value(pTex, VTFTextureWrapper::id);
	return 1;
}
LUA_FUNCTION(VTFTexture_gc)
{
	LUA->CheckType(1, VTFTextureWrapper::id);
	IVTFTexture* pTex = *LUA->GetUserType<IVTFTexture*>(1, VTFTextureWrapper::id);

	LUA->SetUserType(1, NULL);
	delete pTex;

	return 0;
}

LUA_FUNCTION(VTFTexture_IsValid)
{
	IVTFTexture** ppTex = LUA->GetUserType<IVTFTexture*>(1, VTFTextureWrapper::id);
	LUA->PushBool(ppTex != nullptr && (*ppTex)->IsValid());
	return 1;
}

LUA_FUNCTION(VTFTexture_GetWidth)
{
	LUA->CheckType(1, VTFTextureWrapper::id);
	uint8_t mip = LUA->GetNumber(2);

	const IVTFTexture* pTex = *LUA->GetUserType<IVTFTexture*>(1, VTFTextureWrapper::id);
	if (!pTex->IsValid()) LUA->ThrowError("Invalid texture");

	LUA->PushNumber(pTex->GetWidth(mip));
	return 1;
}
LUA_FUNCTION(VTFTexture_GetHeight)
{
	LUA->CheckType(1, VTFTextureWrapper::id);
	uint8_t mip = LUA->GetNumber(2);

	const IVTFTexture* pTex = *LUA->GetUserType<IVTFTexture*>(1, VTFTextureWrapper::id);
	if (!pTex->IsValid()) LUA->ThrowError("Invalid texture");

	LUA->PushNumber(pTex->GetHeight(mip));
	return 1;
}
LUA_FUNCTION(VTFTexture_GetDepth)
{
	LUA->CheckType(1, VTFTextureWrapper::id);
	uint8_t mip = LUA->GetNumber(2);

	const IVTFTexture* pTex = *LUA->GetUserType<IVTFTexture*>(1, VTFTextureWrapper::id);
	if (!pTex->IsValid()) LUA->ThrowError("Invalid texture");

	LUA->PushNumber(pTex->GetDepth(mip));
	return 1;
}

LUA_FUNCTION(VTFTexture_GetFaces)
{
	LUA->CheckType(1, VTFTextureWrapper::id);
	const IVTFTexture* pTex = *LUA->GetUserType<IVTFTexture*>(1, VTFTextureWrapper::id);
	if (!pTex->IsValid()) LUA->ThrowError("Invalid texture");

	LUA->PushNumber(pTex->GetFaces());
	return 1;
}

LUA_FUNCTION(VTFTexture_GetMIPLevels)
{
	LUA->CheckType(1, VTFTextureWrapper::id);
	const IVTFTexture* pTex = *LUA->GetUserType<IVTFTexture*>(1, VTFTextureWrapper::id);
	if (!pTex->IsValid()) LUA->ThrowError("Invalid texture");

	LUA->PushNumber(pTex->GetMIPLevels());
	return 1;
}

LUA_FUNCTION(VTFTexture_GetFrames)
{
	LUA->CheckType(1, VTFTextureWrapper::id);
	const IVTFTexture* pTex = *LUA->GetUserType<IVTFTexture*>(1, VTFTextureWrapper::id);
	if (!pTex->IsValid()) LUA->ThrowError("Invalid texture");

	LUA->PushNumber(pTex->GetFrames());
	return 1;
}
LUA_FUNCTION(VTFTexture_GetFirstFrame)
{
	LUA->CheckType(1, VTFTextureWrapper::id);
	const IVTFTexture* pTex = *LUA->GetUserType<IVTFTexture*>(1, VTFTextureWrapper::id);
	if (!pTex->IsValid()) LUA->ThrowError("Invalid texture");

	LUA->PushNumber(pTex->GetFirstFrame());
	return 1;
}

/*
	uint16_t x
	uint16_t y
	uint16_t z
	uint8_t  mipLevel
	uint16_t frame
	uint8_t  face
*/
LUA_FUNCTION(VTFTexture_GetPixel)
{
	LUA->CheckType(1, VTFTextureWrapper::id);
	const IVTFTexture* pTex = *LUA->GetUserType<IVTFTexture*>(1, VTFTextureWrapper::id);
	if (!pTex->IsValid()) LUA->ThrowError("Invalid texture");

	const int top = LUA->Top();
	const double x        = LUA->CheckNumber(2);
	const double y        = LUA->CheckNumber(3);
	const double z        = LUA->GetNumber(4);
	const double mipLevel = LUA->GetNumber(5);
	const double frame    = LUA->GetNumber(6);
	const double face     = LUA->GetNumber(7);

	if (mipLevel < 0.0 || mipLevel >= pTex->GetMIPLevels())      LUA->ThrowError("MIP level out of range");
	if (x < 0.0        || x        >= pTex->GetWidth(mipLevel))  LUA->ThrowError("x coordinate out of range");
	if (y < 0.0        || y        >= pTex->GetHeight(mipLevel)) LUA->ThrowError("y coordinate out of range");
	if (z < 0.0        || z        >= pTex->GetDepth(mipLevel))  LUA->ThrowError("z coordinate out of range");
	if (frame < 0.0    || frame    >= pTex->GetFrames())         LUA->ThrowError("Frame out of range");
	if (face < 0.0     || face     >= pTex->GetFaces())          LUA->ThrowError("Face out of range");

	Pixel p = pTex->GetPixel(x, y, z, mipLevel, frame, face);
	LUA->PushVector(MakeVector(p.r, p.g, p.b));
	LUA->PushNumber(p.a);
	return 2;
}

/*
	float    u
	float    v
	float    mipLevel
	uint16_t frame
	uint8_t  face
*/
LUA_FUNCTION(VTFTexture_Sample)
{
	LUA->CheckType(1, VTFTextureWrapper::id);
	const IVTFTexture* pTex = *LUA->GetUserType<IVTFTexture*>(1, VTFTextureWrapper::id);
	if (!pTex->IsValid()) LUA->ThrowError("Invalid texture");

	const int top = LUA->Top();
	const double u        = LUA->CheckNumber(2);
	const double v        = LUA->CheckNumber(3);
	const double mipLevel = LUA->GetNumber(4);
	const double frame    = LUA->GetNumber(5);
	const double face     = LUA->GetNumber(6);

	if (frame < 0.0 || frame >= pTex->GetFrames()) LUA->ThrowError("Frame out of range");
	if (face < 0.0  || face >= pTex->GetFaces())   LUA->ThrowError("Face out of range");

	Pixel p = pTex->Sample(u, v, 0, mipLevel, frame, face);
	LUA->PushVector(MakeVector(p.r, p.g, p.b));
	LUA->PushNumber(p.a);
	return 2;
}

LUA_FUNCTION(VTFTexture_tostring)
{
	LUA->PushString("VTFTexture");
	return 1;
}
#pragma endregion

#pragma region Render Targets
/*
	uint8_t            width
	uint8_t            height
	RenderTargetFormat format
	boolean            createMips
*/
LUA_FUNCTION(vistrace_CreateRenderTarget)
{
	uint16_t width = LUA->CheckNumber(1), height = LUA->CheckNumber(2);
	RTFormat format = static_cast<RTFormat>(LUA->CheckNumber(3));

	// Automatically determine correct number of mips to make the Lua API easier
	uint8_t mips = LUA->GetBool(4) ? floorf(log2f(std::max(width, height))) + 1 : 1;

	switch (format) {
	case RTFormat::R8:
	case RTFormat::RG88:
	case RTFormat::RGB888:
	case RTFormat::RF:
	case RTFormat::RGFF:
	case RTFormat::RGBFFF:
		LUA->PushUserType_Value(new RenderTarget(width, height, format, mips), RenderTarget::id);
		return 1;
	default:
		LUA->ArgError(3, "Invalid format");
		return 0; // prevents compiler warning
	}
}
LUA_FUNCTION(RT_gc)
{
	LUA->CheckType(1, RenderTarget::id);
	IRenderTarget* pRt = *LUA->GetUserType<IRenderTarget*>(1, RenderTarget::id);

	LUA->SetUserType(1, NULL);
	delete pRt;

	return 0;
}

LUA_FUNCTION(RT_IsValid)
{
	IRenderTarget** ppRt = LUA->GetUserType<IRenderTarget*>(1, RenderTarget::id);
	LUA->PushBool(ppRt != nullptr && (*ppRt)->IsValid());
	return 1;
}

LUA_FUNCTION(RT_Resize)
{
	LUA->CheckType(1, RenderTarget::id);
	IRenderTarget* pRt = *LUA->GetUserType<IRenderTarget*>(1, RenderTarget::id);
	if (!pRt->IsValid()) LUA->ThrowError("Invalid render target");

	uint16_t width = LUA->CheckNumber(2), height = LUA->CheckNumber(3);

	// Automatically determine correct number of mips to make the Lua API easier
	uint8_t mips = LUA->GetBool(4) ? floorf(log2f(std::max(width, height))) + 1 : 1;

	if (!pRt->Resize(width, height, mips))
		LUA->ThrowError("Failed to resize render target");
	return 0;
}

LUA_FUNCTION(RT_GetWidth)
{
	LUA->CheckType(1, RenderTarget::id);
	uint8_t mip = LUA->GetNumber(2);

	IRenderTarget* pRt = *LUA->GetUserType<IRenderTarget*>(1, RenderTarget::id);
	if (!pRt->IsValid()) LUA->ThrowError("Invalid render target");

	LUA->PushNumber(pRt->GetWidth(mip));
	return 1;
}
LUA_FUNCTION(RT_GetHeight)
{
	LUA->CheckType(1, RenderTarget::id);
	uint8_t mip = LUA->GetNumber(2);

	IRenderTarget* pRt = *LUA->GetUserType<IRenderTarget*>(1, RenderTarget::id);
	if (!pRt->IsValid()) LUA->ThrowError("Invalid render target");

	LUA->PushNumber(pRt->GetHeight(mip));
	return 1;
}
LUA_FUNCTION(RT_GetMIPs)
{
	LUA->CheckType(1, RenderTarget::id);
	IRenderTarget* pRt = *LUA->GetUserType<IRenderTarget*>(1, RenderTarget::id);
	if (!pRt->IsValid()) LUA->ThrowError("Invalid render target");

	LUA->PushNumber(pRt->GetMIPs());
	return 1;
}
LUA_FUNCTION(RT_GetFormat)
{
	LUA->CheckType(1, RenderTarget::id);
	IRenderTarget* pRt = *LUA->GetUserType<IRenderTarget*>(1, RenderTarget::id);
	if (!pRt->IsValid()) LUA->ThrowError("Invalid render target");

	LUA->PushNumber(static_cast<double>(pRt->GetFormat()));
	return 1;
}

LUA_FUNCTION(RT_GetPixel)
{
	LUA->CheckType(1, RenderTarget::id);
	IRenderTarget* pRt = *LUA->GetUserType<IRenderTarget*>(1, RenderTarget::id);
	if (!pRt->IsValid()) LUA->ThrowError("Invalid render target");

	uint16_t x = LUA->CheckNumber(2), y = LUA->CheckNumber(3);
	if (x >= pRt->GetWidth() || y >= pRt->GetHeight()) LUA->ThrowError("Pixel coordinate out of range");

	uint8_t mip = LUA->GetNumber(4); // Returns 0 on failure which is the default mip, so no typechecking needed
	if (mip >= pRt->GetMIPs()) LUA->ThrowError("MIP out of range");

	Pixel pixel = pRt->GetPixel(x, y, mip);

	LUA->PushVector(MakeVector(pixel.r, pixel.g, pixel.b));
	LUA->PushNumber(pixel.a);
	return 2;
}
LUA_FUNCTION(RT_SetPixel)
{
	LUA->CheckType(1, RenderTarget::id);
	IRenderTarget* pRt = *LUA->GetUserType<IRenderTarget*>(1, RenderTarget::id);
	if (!pRt->IsValid()) LUA->ThrowError("Invalid render target");

	uint16_t x = LUA->CheckNumber(2), y = LUA->CheckNumber(3);
	if (x >= pRt->GetWidth() || y >= pRt->GetHeight()) LUA->ThrowError("Pixel coordinate out of range");

	LUA->CheckType(4, Type::Vector);
	Vector rgb = LUA->GetVector(4);

	float a = 1.f;
	if (LUA->IsType(5, Type::Number)) a = LUA->GetNumber(5);

	uint8_t mip = LUA->GetNumber(6); // Returns 0 on failure which is the default mip, so no typechecking needed
	if (mip >= pRt->GetMIPs()) LUA->ThrowError("MIP out of range");

	pRt->SetPixel(x, y, Pixel{ rgb.x, rgb.y, rgb.z, a }, mip);

	return 0;
}

LUA_FUNCTION(RT_GenerateMIPs)
{
	LUA->CheckType(1, RenderTarget::id);
	IRenderTarget* pRt = *LUA->GetUserType<IRenderTarget*>(1, RenderTarget::id);
	if (!pRt->IsValid()) LUA->ThrowError("Invalid render target");

	pRt->GenerateMIPs();
	return 0;
}

LUA_FUNCTION(RT_Save)
{
	LUA->CheckType(1, RenderTarget::id);
	IRenderTarget* pRt = *LUA->GetUserType<IRenderTarget*>(1, RenderTarget::id);
	if (!pRt->IsValid()) LUA->ThrowError("Invalid render target");

	if (!pRt->Save(LUA->CheckString(2), LUA->GetNumber(3)))
		LUA->ThrowError("Failed to save image");
	return 0;
}
LUA_FUNCTION(RT_Load)
{
	LUA->CheckType(1, RenderTarget::id);
	IRenderTarget* pRt = *LUA->GetUserType<IRenderTarget*>(1, RenderTarget::id);
	if (!pRt->IsValid()) LUA->ThrowError("Invalid render target");

	if (!pRt->Load(LUA->CheckString(2), LUA->GetBool(3)))
		LUA->ThrowError("Failed to load image into render target");
	return 0;
}

LUA_FUNCTION(RT_Tonemap)
{
	LUA->CheckType(1, RenderTarget::id);
	IRenderTarget* pRt = *LUA->GetUserType<IRenderTarget*>(1, RenderTarget::id);
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
LUA_FUNCTION(vistrace_CreateAccel)
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
LUA_FUNCTION(AccelStruct_Rebuild)
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
LUA_FUNCTION(AccelStruct_Traverse)
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
LUA_FUNCTION(vistrace_CreateMaterial)
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
	pMat->dielectricInput = glm::clamp(glm::vec3(v.x, v.y, v.z), 0.f, 1.f);
	pMat->conductorInput = pMat->conductor = pMat->dielectric = pMat->dielectricInput;

	return 0;
}

LUA_FUNCTION(Material_DielectricColour)
{
	LUA->CheckType(1, BSDFMaterial::id);
	LUA->CheckType(2, Type::Vector);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(1, BSDFMaterial::id);
	Vector v = LUA->GetVector(2);
	pMat->dielectricInput = pMat->dielectric = glm::clamp(glm::vec3(v.x, v.y, v.z), 0.f, 1.f);
	return 0;
}

LUA_FUNCTION(Material_ConductorColour)
{
	LUA->CheckType(1, BSDFMaterial::id);
	LUA->CheckType(2, Type::Vector);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(1, BSDFMaterial::id);
	Vector v = LUA->GetVector(2);
	pMat->conductorInput = pMat->conductor = glm::clamp(glm::vec3(v.x, v.y, v.z), 0.f, 1.f);
	return 0;
}

LUA_FUNCTION(Material_EdgeTint)
{
	LUA->CheckType(1, BSDFMaterial::id);
	LUA->CheckType(2, Type::Vector);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(1, BSDFMaterial::id);
	Vector v = LUA->GetVector(2);
	pMat->edgetint = glm::clamp(glm::vec3(v.x, v.y, v.z), 0.f, 1.f);
	return 0;
}

LUA_FUNCTION(Material_EdgeTintFalloff)
{
	LUA->CheckType(1, BSDFMaterial::id);
	LUA->CheckType(2, Type::Number);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(1, BSDFMaterial::id);
	pMat->falloff = glm::clamp(LUA->GetNumber(2), 0.001, 1.);
	return 0;
}

LUA_FUNCTION(Material_Metalness)
{
	LUA->CheckType(1, BSDFMaterial::id);
	LUA->CheckType(2, Type::Number);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(1, BSDFMaterial::id);
	pMat->metallic = glm::clamp(LUA->GetNumber(2), 0., 1.);
	pMat->metallicOverridden = true;
	return 0;
}
LUA_FUNCTION(Material_Roughness)
{
	LUA->CheckType(1, BSDFMaterial::id);
	LUA->CheckType(2, Type::Number);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(1, BSDFMaterial::id);
	pMat->linearRoughness = glm::clamp(LUA->GetNumber(2), 0., 1.);
	pMat->roughness = pMat->linearRoughness * pMat->linearRoughness;
	pMat->roughnessOverridden = true;
	return 0;
}

LUA_FUNCTION(Material_Anisotropy)
{
	LUA->CheckType(1, BSDFMaterial::id);
	LUA->CheckType(2, Type::Number);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(1, BSDFMaterial::id);
	// While anisotropy can be mapped -1 to 1
	// with rotation it makes more sense to have it 0-1 (easily stored in a texture) and use rotation to change the direction
	// This is also how blender's principled BSDF node does it
	pMat->anisotropy = glm::clamp(LUA->GetNumber(2), 0., 1. - kMinGGXAlpha);
	pMat->anisotropyOverriden = true;
	return 0;
}
LUA_FUNCTION(Material_AnisotropicRotation)
{
	LUA->CheckType(1, BSDFMaterial::id);
	LUA->CheckType(2, Type::Number);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(1, BSDFMaterial::id);
	pMat->anisotropicRotation = glm::clamp(LUA->GetNumber(2), 0., 1.) * 2. * glm::pi<double>();
	pMat->anisotropicRotationOverriden = true;
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

LUA_FUNCTION(Material_DiffuseTransmission)
{
	LUA->CheckType(1, BSDFMaterial::id);
	LUA->CheckType(2, Type::Number);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(1, BSDFMaterial::id);
	pMat->diffuseTransmission = glm::clamp(LUA->GetNumber(2), 0., 1.);
	return 0;
}
LUA_FUNCTION(Material_SpecularTransmission)
{
	LUA->CheckType(1, BSDFMaterial::id);
	LUA->CheckType(2, Type::Number);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(1, BSDFMaterial::id);
	pMat->specularTransmission = glm::clamp(LUA->GetNumber(2), 0., 1.);
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

LUA_FUNCTION(Material_ActiveLobes)
{
	LUA->CheckType(1, BSDFMaterial::id);
	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(1, BSDFMaterial::id);
	LobeType lobes = static_cast<LobeType>(LUA->CheckNumber(2));

	pMat->activeLobes = lobes;
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
	BSDFSample? sample
*/
LUA_FUNCTION(TraceResult_SampleBSDF)
{
	LUA->CheckType(1, TraceResult::id);
	LUA->CheckType(2, Sampler::id);
	LUA->CheckType(3, BSDFMaterial::id);

	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);
	ISampler* pSampler = *LUA->GetUserType<ISampler*>(2, Sampler::id);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(3, BSDFMaterial::id);
	pMat->PrepShadingData(
		pResult->GetAlbedo(),
		pResult->GetMetalness(), pResult->GetRoughness()
	);

	LUA->Pop(LUA->Top());

	BSDFSample sample;
	bool valid = SampleBSDF(
		*pMat, pSampler,
		pResult->GetNormal(), pResult->GetTangent(), pResult->GetBinormal(),
		pResult->wo,
		sample
	);

	if (!valid) return 0;

	LUA->CreateTable();
	LUA->PushVector(MakeVector(sample.scattered.x, sample.scattered.y, sample.scattered.z));
	LUA->SetField(-2, "scattered");
	LUA->PushVector(MakeVector(sample.weight.x, sample.weight.y, sample.weight.z));
	LUA->SetField(-2, "weight");

	LUA->PushNumber(sample.pdf);
	LUA->SetField(-2, "pdf");

	LUA->PushNumber(static_cast<double>(sample.lobe));
	LUA->SetField(-2, "lobe");

	return 1;
}

/*
	TraceResult  self
	BSDFMaterial material
	Vector       scattered

	returns:
	Vector colour
*/
LUA_FUNCTION(TraceResult_EvalBSDF)
{
	LUA->CheckType(1, TraceResult::id);
	LUA->CheckType(2, BSDFMaterial::id);
	LUA->CheckType(3, Type::Vector);

	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);

	glm::vec3 scattered;
	{
		Vector v = LUA->GetVector(3);
		scattered = glm::vec3(v.x, v.y, v.z);
	}

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(2, BSDFMaterial::id);
	pMat->PrepShadingData(
		pResult->GetAlbedo(),
		pResult->GetMetalness(), pResult->GetRoughness()
	);

	LUA->Pop(LUA->Top());

	glm::vec3 colour = EvalBSDF(
		*pMat,
		pResult->GetNormal(), pResult->GetTangent(), pResult->GetBinormal(),
		pResult->wo, scattered
	);

	LUA->PushVector(MakeVector(colour.x, colour.y, colour.z));
	return 1;
}

/*
	TraceResult  self
	BSDFMaterial material
	Vector       scattered

	returns:
	float pdf
*/
LUA_FUNCTION(TraceResult_EvalPDF)
{
	LUA->CheckType(1, TraceResult::id);
	LUA->CheckType(2, BSDFMaterial::id);
	LUA->CheckType(3, Type::Vector);

	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);

	glm::vec3 scattered;
	{
		Vector v = LUA->GetVector(3);
		scattered = glm::vec3(v.x, v.y, v.z);
	}

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(2, BSDFMaterial::id);
	pMat->PrepShadingData(
		pResult->GetAlbedo(),
		pResult->GetMetalness(), pResult->GetRoughness()
	);

	LUA->Pop(LUA->Top());

	float pdf = EvalPDF(
		*pMat,
		pResult->GetNormal(), pResult->GetTangent(), pResult->GetBinormal(),
		pResult->wo, scattered
	);

	LUA->PushNumber(pdf);
	return 1;
}

/*
	Sampler      sampler
	BSDFMaterial material
	Vector       normal
	Vector       tangent
	Vector       binormal
	Vector       incident

	returns:
	BSDFSample? sample
*/
LUA_FUNCTION(vistrace_SampleBSDF)
{
	LUA->CheckType(1, Sampler::id);
	LUA->CheckType(2, BSDFMaterial::id);
	LUA->CheckType(3, Type::Vector);
	LUA->CheckType(4, Type::Vector);
	LUA->CheckType(5, Type::Vector);
	LUA->CheckType(6, Type::Vector);

	ISampler* pSampler = *LUA->GetUserType<ISampler*>(1, Sampler::id);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(2, BSDFMaterial::id);

	Vector v = LUA->GetVector(3);
	glm::vec3 normal(v.x, v.y, v.z);
	v = LUA->GetVector(4);
	glm::vec3 tangent(v.x, v.y, v.z);
	v = LUA->GetVector(5);
	glm::vec3 binormal(v.x, v.y, v.z);
	v = LUA->GetVector(6);
	glm::vec3 incident(v.x, v.y, v.z);

	LUA->Pop(LUA->Top());

	BSDFSample sample;
	bool valid = SampleBSDF(
		*pMat, pSampler,
		normal, tangent, binormal,
		incident,
		sample
	);

	if (!valid) return 0;

	LUA->CreateTable();
	LUA->PushVector(MakeVector(sample.scattered.x, sample.scattered.y, sample.scattered.z));
	LUA->SetField(-2, "scattered");
	LUA->PushVector(MakeVector(sample.weight.x, sample.weight.y, sample.weight.z));
	LUA->SetField(-2, "weight");

	LUA->PushNumber(sample.pdf);
	LUA->SetField(-2, "pdf");

	LUA->PushNumber(static_cast<double>(sample.lobe));
	LUA->SetField(-2, "lobe");

	return 1;
}

/*
	BSDFMaterial material
	Vector       normal
	Vector       tangent
	Vector       binormal
	Vector       incident
	Vector       scattered

	returns:
	Vector colour
*/
LUA_FUNCTION(vistrace_EvalBSDF)
{
	LUA->CheckType(1, BSDFMaterial::id);
	LUA->CheckType(2, Type::Vector);
	LUA->CheckType(3, Type::Vector);
	LUA->CheckType(4, Type::Vector);
	LUA->CheckType(5, Type::Vector);
	LUA->CheckType(6, Type::Vector);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(1, BSDFMaterial::id);

	Vector v = LUA->GetVector(2);
	glm::vec3 normal(v.x, v.y, v.z);
	v = LUA->GetVector(3);
	glm::vec3 tangent(v.x, v.y, v.z);
	v = LUA->GetVector(4);
	glm::vec3 binormal(v.x, v.y, v.z);
	v = LUA->GetVector(5);
	glm::vec3 incident(v.x, v.y, v.z);
	v = LUA->GetVector(6);
	glm::vec3 scattered(v.x, v.y, v.z);

	LUA->Pop(LUA->Top());

	glm::vec3 colour = EvalBSDF(*pMat, normal, tangent, binormal, incident, scattered);
	LUA->PushVector(MakeVector(colour.x, colour.y, colour.z));
	return 1;
}

/*
	BSDFMaterial material
	Vector       normal
	Vector       tangent
	Vector       binormal
	Vector       incident
	Vector       scattered

	returns:
	float pdf
*/
LUA_FUNCTION(vistrace_EvalPDF)
{
	LUA->CheckType(1, BSDFMaterial::id);
	LUA->CheckType(2, Type::Vector);
	LUA->CheckType(3, Type::Vector);
	LUA->CheckType(4, Type::Vector);
	LUA->CheckType(5, Type::Vector);
	LUA->CheckType(6, Type::Vector);

	BSDFMaterial* pMat = LUA->GetUserType<BSDFMaterial>(1, BSDFMaterial::id);

	Vector v = LUA->GetVector(2);
	glm::vec3 normal(v.x, v.y, v.z);
	v = LUA->GetVector(3);
	glm::vec3 tangent(v.x, v.y, v.z);
	v = LUA->GetVector(4);
	glm::vec3 binormal(v.x, v.y, v.z);
	v = LUA->GetVector(5);
	glm::vec3 incident(v.x, v.y, v.z);
	v = LUA->GetVector(6);
	glm::vec3 scattered(v.x, v.y, v.z);

	LUA->Pop(LUA->Top());

	float pdf = EvalPDF(*pMat, normal, tangent, binormal, incident, scattered);
	LUA->PushNumber(pdf);
	return 1;
}
#pragma endregion

#pragma region HDRI API
LUA_FUNCTION(vistrace_LoadHDRI)
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
LUA_FUNCTION(vistrace_CalcRayOrigin)
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
	LUA->Pop(3); // _G, game, and string

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

#define PUSH_C_FUNC(prefix, name) LUA->PushCFunction(prefix##_##name); LUA->SetField(-2, #name)

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

	VTFTextureWrapper::id = LUA->CreateMetaTable("VisTraceVTFTexture");
	LUA->PushSpecial(SPECIAL_REG);
	LUA->PushNumber(VTFTextureWrapper::id);
	LUA->SetField(-2, "VisTraceVTFTexture_id");
	LUA->Pop(); // Pop the registry
		LUA->Push(-1);
		LUA->SetField(-2, "__index");
		LUA->PushCFunction(VTFTexture_tostring);
		LUA->SetField(-2, "__tostring");
		LUA->PushCFunction(VTFTexture_gc);
		LUA->SetField(-2, "__gc");

		PUSH_C_FUNC(VTFTexture, IsValid);

		PUSH_C_FUNC(VTFTexture, GetWidth);
		PUSH_C_FUNC(VTFTexture, GetHeight);
		PUSH_C_FUNC(VTFTexture, GetDepth);

		PUSH_C_FUNC(VTFTexture, GetFaces);

		PUSH_C_FUNC(VTFTexture, GetMIPLevels);

		PUSH_C_FUNC(VTFTexture, GetFrames);
		PUSH_C_FUNC(VTFTexture, GetFirstFrame);

		PUSH_C_FUNC(VTFTexture, GetPixel);
		PUSH_C_FUNC(VTFTexture, Sample);
	LUA->Pop();

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

		PUSH_C_FUNC(RT, IsValid);

		PUSH_C_FUNC(RT, Resize);

		PUSH_C_FUNC(RT, GetWidth);
		PUSH_C_FUNC(RT, GetHeight);
		PUSH_C_FUNC(RT, GetFormat);

		PUSH_C_FUNC(RT, GetPixel);
		PUSH_C_FUNC(RT, SetPixel);

		PUSH_C_FUNC(RT, GenerateMIPs);

		PUSH_C_FUNC(RT, Save);
		PUSH_C_FUNC(RT, Load);

		PUSH_C_FUNC(RT, Tonemap);
	LUA->Pop();

	TraceResult::id = LUA->CreateMetaTable("VisTraceResult");
		LUA->Push(-1);
		LUA->SetField(-2, "__index");
		LUA->PushCFunction(TraceResult_tostring);
		LUA->SetField(-2, "__tostring");

		PUSH_C_FUNC(TraceResult, Pos);
		PUSH_C_FUNC(TraceResult, Distance);

		PUSH_C_FUNC(TraceResult, Entity);

		PUSH_C_FUNC(TraceResult, GeometricNormal);
		PUSH_C_FUNC(TraceResult, Normal);
		PUSH_C_FUNC(TraceResult, Tangent);
		PUSH_C_FUNC(TraceResult, Binormal);

		PUSH_C_FUNC(TraceResult, Barycentric);
		PUSH_C_FUNC(TraceResult, TextureUV);

		PUSH_C_FUNC(TraceResult, SubMaterialIndex);

		PUSH_C_FUNC(TraceResult, Albedo);
		PUSH_C_FUNC(TraceResult, Alpha);
		PUSH_C_FUNC(TraceResult, Metalness);
		PUSH_C_FUNC(TraceResult, Roughness);

		PUSH_C_FUNC(TraceResult, MaterialFlags);
		PUSH_C_FUNC(TraceResult, SurfaceFlags);

		PUSH_C_FUNC(TraceResult, HitSky);
		PUSH_C_FUNC(TraceResult, HitWater);

		PUSH_C_FUNC(TraceResult, FrontFacing);

		PUSH_C_FUNC(TraceResult, BaseMIPLevel);

		PUSH_C_FUNC(TraceResult, SampleBSDF);
		PUSH_C_FUNC(TraceResult, EvalBSDF);
		PUSH_C_FUNC(TraceResult, EvalPDF);
	LUA->Pop();

	AccelStruct_id = LUA->CreateMetaTable("AccelStruct");
		LUA->Push(-1);
		LUA->SetField(-2, "__index");
		LUA->PushCFunction(AccelStruct_tostring);
		LUA->SetField(-2, "__tostring");
		LUA->PushCFunction(AccelStruct_gc);
		LUA->SetField(-2, "__gc");

		PUSH_C_FUNC(AccelStruct, Traverse);
		PUSH_C_FUNC(AccelStruct, Rebuild);
	LUA->Pop();

	Sampler::id = LUA->CreateMetaTable("Sampler");
	LUA->PushSpecial(SPECIAL_REG);
	LUA->PushNumber(Sampler::id);
	LUA->SetField(-2, "Sampler_id");
	LUA->Pop(); // Pop the registry
		LUA->Push(-1);
		LUA->SetField(-2, "__index");
		LUA->PushCFunction(Sampler_tostring);
		LUA->SetField(-2, "__tostring");
		LUA->PushCFunction(Sampler_gc);
		LUA->SetField(-2, "__gc");

		PUSH_C_FUNC(Sampler, GetFloat);
		PUSH_C_FUNC(Sampler, GetFloat2D);
	LUA->Pop();

	HDRI::id = LUA->CreateMetaTable("HDRI");
		LUA->Push(-1);
		LUA->SetField(-2, "__index");
		LUA->PushCFunction(HDRI_tostring);
		LUA->SetField(-2, "__tostring");
		LUA->PushCFunction(HDRI_gc);
		LUA->SetField(-2, "__gc");

		PUSH_C_FUNC(HDRI, IsValid);

		PUSH_C_FUNC(HDRI, GetPixel);

		PUSH_C_FUNC(HDRI, EvalPDF);
		PUSH_C_FUNC(HDRI, Sample);

		PUSH_C_FUNC(HDRI, SetAngles);
	LUA->Pop();

	BSDFMaterial::id = LUA->CreateMetaTable("BSDFMaterial");
		LUA->Push(-1);
		LUA->SetField(-2, "__index");
		LUA->PushCFunction(Material_tostring);
		LUA->SetField(-2, "__tostring");

		PUSH_C_FUNC(Material, Colour);
		PUSH_C_FUNC(Material, DielectricColour);
		PUSH_C_FUNC(Material, ConductorColour);

		PUSH_C_FUNC(Material, EdgeTint);
		PUSH_C_FUNC(Material, EdgeTintFalloff);

		PUSH_C_FUNC(Material, Metalness);
		PUSH_C_FUNC(Material, Roughness);
		PUSH_C_FUNC(Material, Anisotropy);
		PUSH_C_FUNC(Material, AnisotropicRotation);

		PUSH_C_FUNC(Material, IoR);

		PUSH_C_FUNC(Material, DiffuseTransmission);
		PUSH_C_FUNC(Material, SpecularTransmission);

		PUSH_C_FUNC(Material, Thin);

		PUSH_C_FUNC(Material, ActiveLobes);
	LUA->Pop();

	LUA->PushSpecial(SPECIAL_GLOB);
		LUA->CreateTable();
			PUSH_C_FUNC(vistrace, CreateRenderTarget);
			PUSH_C_FUNC(vistrace, CreateAccel);
			PUSH_C_FUNC(vistrace, CreateSampler);
			PUSH_C_FUNC(vistrace, CreateMaterial);

			PUSH_C_FUNC(vistrace, LoadHDRI);
			PUSH_C_FUNC(vistrace, LoadTexture);

			PUSH_C_FUNC(vistrace, CalcRayOrigin);

			PUSH_C_FUNC(vistrace, SampleBSDF);
			PUSH_C_FUNC(vistrace, EvalBSDF);
			PUSH_C_FUNC(vistrace, EvalPDF);
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

		LUA->CreateTable();
			PUSH_ENUM(LobeType, None);

			PUSH_ENUM(LobeType, DiffuseReflection);
			PUSH_ENUM(LobeType, DiffuseTransmission);

			PUSH_ENUM(LobeType, SpecularReflection);
			PUSH_ENUM(LobeType, SpecularTransmission);
			PUSH_ENUM(LobeType, ConductiveReflection);

			PUSH_ENUM(LobeType, DeltaSpecularReflection);
			PUSH_ENUM(LobeType, DeltaSpecularTransmission);
			PUSH_ENUM(LobeType, DeltaConductiveReflection);

			PUSH_ENUM(LobeType, Reflection);
			PUSH_ENUM(LobeType, Transmission);

			PUSH_ENUM(LobeType, Delta);
			PUSH_ENUM(LobeType, NonDelta);

			PUSH_ENUM(LobeType, Diffuse);
			PUSH_ENUM(LobeType, Specular);
			PUSH_ENUM(LobeType, Dielectric);
			PUSH_ENUM(LobeType, Conductive);

			PUSH_ENUM(LobeType, All);
		LUA->SetField(-2, "LobeType");
	LUA->Pop();

	LUA->PushSpecial(SPECIAL_GLOB);
	LUA->GetField(-1, "game");
	LUA->GetField(-1, "GetMap");
	LUA->Call(0, 1);
	const std::string mapName = LUA->GetString();
	LUA->Pop(2); // game and string

	/*
		According to the wiki:
		> In Multiplayer this does not return the current map in the CLIENT realm before GM:Initialize.

		However in testing on a local server I was unable to actually get it to return an invalid value,
		but this is here just in case (checking against menu, we'll see if any map mismatches appear)
	*/
	if (mapName == "menu") {
		LUA->GetField(-1, "hook");
		LUA->GetField(-1, "Add");
		LUA->PushString("Initialize");
		LUA->PushString("VisTrace.LoadWorld");
		LUA->PushCFunction(GM_Initialize);
		LUA->Call(3, 0);
		LUA->Pop(2); // hook and _G
	} else { // Map is valid, call the initialise hook directly
		LUA->Pop(); // _G
		GM_Initialize__Imp(LUA);
	}

	printLua(LUA, "VisTrace Loaded!");
	return 0;
}

GMOD_MODULE_CLOSE()
{
	if (g_pWorld != nullptr) delete g_pWorld;
	return 0;
}
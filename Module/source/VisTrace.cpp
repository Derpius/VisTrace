#include "TraceResult.h"
#include "AccelStruct.h"
#include "Utils.h"

#include "VTFParser.h"
#include "BSPParser.h"
#include "GMFS.h"

#include "BSDF.h"
#include "HDRI.h"
#include "Sampler.h"

using namespace GarrysMod::Lua;

#pragma region Sampler
static int Sampler_id;

LUA_FUNCTION(CreateSampler)
{
	uint32_t seed = time(NULL);
	if (LUA->IsType(1, Type::Number)) seed = LUA->GetNumber(1);

	Sampler* pSampler = new Sampler(seed);
	LUA->PushUserType_Value(pSampler, Sampler_id);
	return 1;
}

LUA_FUNCTION(Sampler_gc)
{
	LUA->CheckType(1, Sampler_id);
	Sampler* pSampler = *LUA->GetUserType<Sampler*>(1, Sampler_id);

	LUA->SetUserType(1, NULL);
	delete pSampler;

	return 0;
}

LUA_FUNCTION(Sampler_GetFloat)
{
	LUA->CheckType(1, Sampler_id);
	Sampler* pSampler = *LUA->GetUserType<Sampler*>(1, Sampler_id);
	LUA->PushNumber(pSampler->GetFloat());
	return 1;
}

LUA_FUNCTION(Sampler_GetFloat2D)
{
	LUA->CheckType(1, Sampler_id);
	Sampler* pSampler = *LUA->GetUserType<Sampler*>(1, Sampler_id);

	glm::vec2 sample = pSampler->GetFloat2D();
	LUA->PushNumber(sample.x);
	LUA->PushNumber(sample.y);
	return 2;
}

LUA_FUNCTION(Sampler_tostring)
{
	LUA->PushString("Sampler");
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

	LUA->PushVector(MakeVector(pResult->GetGeometricNormal().x, pResult->GetGeometricNormal().y, pResult->GetGeometricNormal().z));
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
	bool        hitWorld = true
	bool        hitWater = false

	returns modified TraceResult struct (https://wiki.facepunch.com/gmod/Structures/TraceResult)
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

#pragma region BxDF API
MaterialProperties ReadMaterialProps(ILuaBase* LUA, const int stackPos, const glm::vec3& outgoing, const glm::vec3& normal)
{
	using namespace glm;
	MaterialProperties material{};

	LUA->GetField(stackPos, "Metalness");
	if (LUA->IsType(-1, Type::Number)) {
		material.metallic = glm::clamp(LUA->GetNumber(), 0., 1.);
	}
	LUA->GetField(stackPos, "Roughness");
	if (LUA->IsType(-1, Type::Number)) {
		material.roughness = LUA->GetNumber();
	}
	LUA->Pop(2);

	float ior = 1.f;
	LUA->GetField(stackPos, "IoR");
	if (LUA->IsType(-1, Type::Number)) {
		ior = LUA->GetNumber();
	}
	LUA->Pop();

	float f = (ior - 1.f) / (ior + 1.f);
	float F0 = f * f;

	LUA->GetField(stackPos, "BaseColour");
	if (LUA->IsType(-1, Type::Vector)) {
		Vector v = LUA->GetVector(-1);
		material.diffuse = mix(glm::vec3(v.x, v.y, v.z), vec3(0.f), material.metallic);
	} else material.diffuse = mix(glm::vec3(1.f), vec3(0.f), material.metallic);
	material.specular = mix(vec3(F0), material.diffuse, material.metallic);

	LUA->GetField(stackPos, "TransmissionColour");
	if (LUA->IsType(-1, Type::Vector)) {
		Vector v = LUA->GetVector(-1);
		material.transmission = glm::vec3(v.x, v.y, v.z);
	} else material.transmission = material.specular;
	LUA->Pop(2);

	LUA->GetField(stackPos, "SpecularTransmission");
	if (LUA->IsType(-1, Type::Number)) {
		material.specularTransmission = glm::clamp(LUA->GetNumber(), 0., 1.);
	}
	LUA->GetField(stackPos, "DiffuseTransmission");
	if (LUA->IsType(-1, Type::Number)) {
		material.diffuseTransmission = glm::clamp(LUA->GetNumber(), 0., 1.);
	}
	LUA->Pop(2);

	LUA->GetField(stackPos, "RelativeIoR");
	if (LUA->IsType(-1, Type::Number)) {
		material.eta = LUA->GetNumber();
	} else material.eta = glm::dot(outgoing, normal) < 0.f ? ior : 1.f / ior;
	LUA->Pop();

	LUA->GetField(stackPos, "Thin");
	if (LUA->IsType(-1, Type::Bool)) {
		material.thin = LUA->GetBool();
	}
	LUA->Pop();

	return material;
}

/*
	TraceResult self
	Sampler     sampler
	table       material (optional fields corresponding to MaterialParameter members)

	returns:
	bool valid
	BSDFSample? sample
*/
LUA_FUNCTION(TraceResult_SampleBSDF)
{
	LUA->CheckType(1, TraceResult::id);
	LUA->CheckType(2, Sampler_id);
	LUA->CheckType(3, Type::Table);

	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);
	Sampler* pSampler = *LUA->GetUserType<Sampler*>(2, Sampler_id);

	auto material = ReadMaterialProps(LUA, 3, pResult->wo, pResult->GetNormal());

	LUA->Pop(LUA->Top());

	BSDFSample sample;
	bool valid = SampleFalcorBSDF(
		material, pSampler, sample,
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
	TraceResult self
	table       material (optional fields corresponding to MaterialParameter members)
	Vector      wi

	returns:
	Vector colour
*/
LUA_FUNCTION(TraceResult_EvalBSDF)
{
	LUA->CheckType(1, TraceResult::id);
	LUA->CheckType(2, Type::Table);
	LUA->CheckType(3, Type::Vector);

	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);

	glm::vec3 incoming;
	{
		Vector v = LUA->GetVector(3);
		incoming = glm::vec3(v.x, v.y, v.z);
	}

	auto material = ReadMaterialProps(LUA, 2, pResult->wo, pResult->GetNormal());

	LUA->Pop(LUA->Top());

	glm::vec3 colour = EvalFalcorBSDF(
		material,
		pResult->GetNormal(), pResult->GetTangent(), pResult->GetBinormal(),
		pResult->wo, incoming
	);

	LUA->PushVector(MakeVector(colour.x, colour.y, colour.z));
	return 1;
}

/*
	TraceResult self
	table       material (optional fields corresponding to MaterialParameter members)
	Vector      wi

	returns:
	float pdf
*/
LUA_FUNCTION(TraceResult_EvalPDF)
{
	LUA->CheckType(1, TraceResult::id);
	LUA->CheckType(2, Type::Table);
	LUA->CheckType(3, Type::Vector);

	TraceResult* pResult = LUA->GetUserType<TraceResult>(1, TraceResult::id);

	glm::vec3 incoming;
	{
		Vector v = LUA->GetVector(3);
		incoming = glm::vec3(v.x, v.y, v.z);
	}

	auto material = ReadMaterialProps(LUA, 2, pResult->wo, pResult->GetNormal());

	LUA->Pop(LUA->Top());

	float pdf = EvalPDFFalcorBSDF(
		material,
		pResult->GetNormal(), pResult->GetTangent(), pResult->GetBinormal(),
		pResult->wo, incoming
	);

	LUA->PushNumber(pdf);
	return 1;
}
#pragma endregion

#pragma region HDRI API
static int HDRI_id;

LUA_FUNCTION(LoadHDRI)
{
	LUA->CheckString(1);

	float radianceThresh = 1000.f;
	if (LUA->IsType(2, Type::Number)) {
		radianceThresh = LUA->GetNumber(2);
	}

	uint32_t areaThresh = 8 * 8;
	if (LUA->IsType(3, Type::Number)) {
		areaThresh = LUA->GetNumber(3);
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

	HDRI* pHDRI = new HDRI(data, filesize, radianceThresh, areaThresh);
	LUA->PushUserType_Value(pHDRI, HDRI_id);
	free(data);

	return 1;
}

LUA_FUNCTION(HDRI_IsValid)
{
	LUA->CheckType(1, HDRI_id);
	HDRI* pHDRI = *LUA->GetUserType<HDRI*>(1, HDRI_id);
	LUA->PushBool(pHDRI->IsValid());
	return 1;
}

LUA_FUNCTION(HDRI_GetPixel)
{
	LUA->CheckType(1, HDRI_id);
	LUA->CheckType(2, Type::Vector);

	HDRI* pHDRI = *LUA->GetUserType<HDRI*>(1, HDRI_id);
	Vector direction = LUA->GetVector(2);

	glm::vec4 colour = pHDRI->GetPixel(glm::vec3(direction.x, direction.y, direction.z));
	LUA->PushVector(MakeVector(colour.r, colour.g, colour.b));
	LUA->PushNumber(colour.a); // Radiance
	return 2;
}

LUA_FUNCTION(HDRI_EvalPDF)
{
	LUA->CheckType(1, HDRI_id);
	LUA->CheckType(2, Type::Vector);

	HDRI* pHDRI = *LUA->GetUserType<HDRI*>(1, HDRI_id);
	Vector direction = LUA->GetVector(2);

	float pdf = pHDRI->EvalPDF(glm::vec3(direction.x, direction.y, direction.z));
	LUA->PushNumber(pdf);
	return 1;
}

LUA_FUNCTION(HDRI_Sample)
{
	LUA->CheckType(1, HDRI_id);
	LUA->CheckType(2, Sampler_id);

	HDRI* pHDRI = *LUA->GetUserType<HDRI*>(1, HDRI_id);
	Sampler* pSampler = *LUA->GetUserType<Sampler*>(2, Sampler_id);

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
	LUA->CheckType(1, HDRI_id);
	LUA->CheckType(2, Type::Angle);

	HDRI* pHDRI = *LUA->GetUserType<HDRI*>(1, HDRI_id);
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
	LUA->CheckType(1, HDRI_id);
	HDRI* pHDRI = *LUA->GetUserType<HDRI*>(1, HDRI_id);
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

#define PUSH_C_FUNC(function) LUA->PushCFunction(function); LUA->SetField(-2, #function)

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
		LUA->ThrowError("Failed to load map, accelerations structures will only trace props");
	}

	printLua(LUA, "VisTrace: Map loaded successfully!");
	return 0;
}

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

	TraceResult::id = LUA->CreateMetaTable("VisTraceResult");
		LUA->Push(-1);
		LUA->SetField(-2, "__index");

		LUA->PushCFunction(TraceResult_Pos);
		LUA->SetField(-2, "Pos");

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
		LUA->PushCFunction(AccelStruct_gc);
		LUA->SetField(-2, "__gc");
		LUA->PushCFunction(RebuildAccel);
		LUA->SetField(-2, "Rebuild");
		LUA->PushCFunction(TraverseScene);
		LUA->SetField(-2, "Traverse");
		LUA->PushCFunction(AccelStruct_tostring);
		LUA->SetField(-2, "__tostring");
	LUA->Pop();

	Sampler_id = LUA->CreateMetaTable("Sampler");
		LUA->Push(-1);
		LUA->SetField(-2, "__index");
		LUA->PushCFunction(Sampler_gc);
		LUA->SetField(-2, "__gc");
		LUA->PushCFunction(Sampler_GetFloat);
		LUA->SetField(-2, "GetFloat");
		LUA->PushCFunction(Sampler_GetFloat2D);
		LUA->SetField(-2, "GetFloat2D");
		LUA->PushCFunction(Sampler_tostring);
		LUA->SetField(-2, "__tostring");
	LUA->Pop();

	HDRI_id = LUA->CreateMetaTable("HDRI");
		LUA->Push(-1);
		LUA->SetField(-2, "__index");
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
		LUA->PushCFunction(HDRI_tostring);
		LUA->SetField(-2, "__tostring");
		LUA->PushCFunction(HDRI_gc);
		LUA->SetField(-2, "__gc");
	LUA->Pop();

	LUA->PushSpecial(SPECIAL_GLOB);
		LUA->CreateTable();
			PUSH_C_FUNC(CreateAccel);
			PUSH_C_FUNC(CreateSampler);
			PUSH_C_FUNC(LoadHDRI);

			PUSH_C_FUNC(CalcRayOrigin);
		LUA->SetField(-2, "vistrace");
	LUA->Pop();

	printLua(LUA, "VisTrace Loaded!");
	return 0;
}

GMOD_MODULE_CLOSE()
{
	if (g_pWorld != nullptr) delete g_pWorld;
	return 0;
}
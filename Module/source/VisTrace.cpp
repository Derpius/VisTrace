#include "AccelStruct.h"
#include "Utils.h"

#include "filesystem.h"
#include "GarrysMod/InterfacePointers.hpp"

#include "VTFParser.h"

#include "BSDF.h"
#include "HDRI.h"

using namespace GarrysMod::Lua;

#pragma region Tracing API
static int AccelStruct_id;
static IFileSystem* pFileSystem;

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

	returns AccelStruct
*/
LUA_FUNCTION(CreateAccel)
{
	AccelStruct* pAccelStruct = new AccelStruct(pFileSystem);

	if (LUA->Top() == 0) LUA->CreateTable();
	else if (LUA->IsType(1, Type::Nil)) {
		LUA->Pop(LUA->Top());
		LUA->CreateTable();
	} else {
		LUA->CheckType(1, Type::Table);
		LUA->Pop(LUA->Top() - 1); // Pop all but the table
	}
	pAccelStruct->PopulateAccel(LUA);

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
	AccelStruct* pAccelStruct = *LUA->GetUserType<AccelStruct*>(1, AccelStruct_id);

	if (LUA->Top() == 1) LUA->CreateTable();
	else if (LUA->IsType(2, Type::Nil)) {
		LUA->Pop(LUA->Top());
		LUA->CreateTable();
	} else {
		LUA->CheckType(2, Type::Table);
		LUA->Pop(LUA->Top() - 2); // Pop all but the table (and self)
	}
	pAccelStruct->PopulateAccel(LUA);

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

	pAccelStruct->Traverse(LUA);

	return 1; // Return the table at the top of the stack (this will either be a default TraceResult, a TraceResult populated by BVH intersection, or a TraceResult populated by world intersection)
}

LUA_FUNCTION(AccelStruct_tostring)
{
	LUA->PushString("AccelStruct");
	return 1;
}
#pragma endregion

#pragma region BxDF API
static int Sampler_id;

LUA_FUNCTION(CreateSampler)
{
	uint32_t seed = time(NULL);
	if (LUA->IsType(1, Type::Number)) seed = LUA->GetNumber(1);
	LUA->PushUserType_Value(BSDFSampler(seed), Sampler_id);
	return 1;
}

LUA_FUNCTION(Sampler_GetFloat)
{
	LUA->CheckType(1, Sampler_id);
	BSDFSampler* pSampler = LUA->GetUserType<BSDFSampler>(1, Sampler_id);
	LUA->PushNumber(pSampler->GetFloat());
	return 1;
}

LUA_FUNCTION(Sampler_tostring)
{
	LUA->PushString("Sampler");
	return 1;
}

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
	BSDFSampler sampler
	table       material (optional fields corresponding to MaterialParameter members)
	Vector      normal
	Vector      tangent
	Vector      binormal
	Vector      wo

	returns:
	bool valid
	BSDFSample? sample
*/
LUA_FUNCTION(SampleBSDF)
{
	LUA->CheckType(1, Sampler_id);
	LUA->CheckType(2, Type::Table);
	LUA->CheckType(3, Type::Vector);
	LUA->CheckType(4, Type::Vector);
	LUA->CheckType(5, Type::Vector);
	LUA->CheckType(6, Type::Vector);

	BSDFSampler* pSampler = LUA->GetUserType<BSDFSampler>(1, Sampler_id);

	glm::vec3 normal, tangent, binormal, outgoing;
	{
		Vector v = LUA->GetVector(3);
		normal = glm::vec3(v.x, v.y, v.z);

		v = LUA->GetVector(4);
		tangent = glm::vec3(v.x, v.y, v.z);

		v = LUA->GetVector(5);
		binormal = glm::vec3(v.x, v.y, v.z);

		v = LUA->GetVector(6);
		outgoing = glm::vec3(v.x, v.y, v.z);
	}

	auto material = ReadMaterialProps(LUA, 2, outgoing, normal);

	LUA->Pop(LUA->Top());

	BSDFSample sample;
	bool valid = SampleFalcorBSDF(material, pSampler, sample, normal, tangent, binormal, outgoing);

	if (!valid) {
		LUA->PushBool(false);
		return 1;
	}

	LUA->PushBool(true);

	LUA->CreateTable();
	LUA->PushVector(Vector{ sample.wo.x, sample.wo.y, sample.wo.z });
	LUA->SetField(-2, "wo");
	LUA->PushVector(Vector{ sample.weight.x, sample.weight.y, sample.weight.z });
	LUA->SetField(-2, "weight");

	LUA->PushNumber(sample.pdf);
	LUA->SetField(-2, "pdf");

	LUA->PushNumber(sample.lobe);
	LUA->SetField(-2, "lobe");

	return 2;
}

/*
	table  material (optional fields corresponding to MaterialParameter members)
	Vector normal
	Vector tangent
	Vector binormal
	Vector wo
	Vector wi

	returns:
	Vector colour
	float  forwardPdf
	float  reversePdf

*/
LUA_FUNCTION(EvaluateBSDF)
{
	LUA->CheckType(1, Type::Table);
	LUA->CheckType(2, Type::Vector);
	LUA->CheckType(3, Type::Vector);
	LUA->CheckType(4, Type::Vector);
	LUA->CheckType(5, Type::Vector);
	LUA->CheckType(6, Type::Vector);

	glm::vec3 normal, tangent, binormal, outgoing, incoming;
	{
		Vector v = LUA->GetVector(2);
		normal = glm::vec3(v.x, v.y, v.z);

		v = LUA->GetVector(3);
		tangent = glm::vec3(v.x, v.y, v.z);

		v = LUA->GetVector(4);
		binormal = glm::vec3(v.x, v.y, v.z);

		v = LUA->GetVector(5);
		outgoing = glm::vec3(v.x, v.y, v.z);

		v = LUA->GetVector(6);
		incoming = glm::vec3(v.x, v.y, v.z);
	}

	auto material = ReadMaterialProps(LUA, 1, outgoing, normal);

	LUA->Pop(LUA->Top());

	glm::vec3 colour = EvalFalcorBSDF(material, normal, tangent, binormal, outgoing, incoming);

	LUA->PushVector(Vector{ colour.x, colour.y, colour.z });
	return 1;
}
#pragma endregion

#pragma region HDRI API
static int HDRI_id;

LUA_FUNCTION(LoadHDRI)
{
	LUA->CheckString(1);

	std::string texturePath = "materials/vistrace/hdris/" + std::string(LUA->GetString(1)) + ".png";
	if (!pFileSystem->FileExists(texturePath.c_str(), "GAME"))
		LUA->ThrowError("HDRI file does not exist");
	FileHandle_t file = pFileSystem->Open(texturePath.c_str(), "rb", "GAME");

	uint32_t filesize = pFileSystem->Size(file);
	uint8_t* data = reinterpret_cast<uint8_t*>(malloc(filesize));
	if (data == nullptr) LUA->ThrowError("Failed to allocate memory for HDRI");

	pFileSystem->Read(data, filesize, file);
	pFileSystem->Close(file);

	HDRI* pHDRI = new HDRI(data, filesize);
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
	LUA->PushVector(Vector(colour.r, colour.g, colour.b));
	LUA->PushNumber(colour.a); // Radiance
	return 2;
}

LUA_FUNCTION(HDRI_Sample)
{
	return 0;
}

LUA_FUNCTION(HDRI_SetAngles)
{
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
	;

	LUA->PushVector(Vector(
		abs(pos.x) < origin ? pos.x + fOff.x : iPos.x,
		abs(pos.y) < origin ? pos.y + fOff.y : iPos.y,
		abs(pos.z) < origin ? pos.z + fOff.z : iPos.z
	));
	return 1;
}
#pragma endregion

#define PUSH_C_FUNC(function) LUA->PushCFunction(function); LUA->SetField(-2, #function)

GMOD_MODULE_OPEN()
{
	pFileSystem = InterfacePointers::FileSystem();
	if (pFileSystem == nullptr) LUA->ThrowError("Failed to get filesystem");

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
		LUA->PushCFunction(Sampler_GetFloat);
		LUA->SetField(-2, "GetFloat");
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

			PUSH_C_FUNC(SampleBSDF);
			PUSH_C_FUNC(EvaluateBSDF);

			PUSH_C_FUNC(CalcRayOrigin);
		LUA->SetField(-2, "vistrace");
	LUA->Pop();

	printLua(LUA, "VisTrace Loaded!");
	return 0;
}

GMOD_MODULE_CLOSE()
{
	return 0;
}
#include <stdexcept>

#include "GMFS.h"

#include "AccelStruct.h"
#include "Utils.h"

#include "TraceResult.h"

#include "ResourceCache.h"
#include "Model.h"

#include "bvh/locally_ordered_clustering_builder.hpp"
#include "bvh/leaf_collapser.hpp"

#include "glm/gtx/euler_angles.hpp"

#define MISSING_TEXTURE "debug/debugempty"
#define WATER_BASE_TEXTURE "models/debug/debugwhite"

#define MISSING_MODEL "models/error.mdl"

using namespace GarrysMod::Lua;
using namespace VisTrace;

void normalise(Vector3& v)
{
	float length = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	v[0] /= length;
	v[1] /= length;
	v[2] /= length;
}

glm::vec3 TransformToBone(
	const glm::vec3& vec,
	const std::vector<glm::mat4>& bones, const std::vector<glm::mat4>& binds,
	uint8_t numBones, float weights[3], int8_t boneIds[3],
	const bool angleOnly = false
)
{
	glm::vec4 final(0.f);
	glm::vec4 vertex = glm::vec4(vec, angleOnly ? 0.f : 1.f);
	for (size_t i = 0U; i < numBones; i++) {
		final += bones[boneIds[i]] * binds[boneIds[i]] * vertex * weights[i];
	}
	return glm::vec3(final);
}

Vector3 TransformToBone(
	const Vector3& vec,
	const std::vector<glm::mat4>& bones, const std::vector<glm::mat4>& binds,
	uint8_t numBones, float weights[3], int8_t boneIds[3],
	const bool angleOnly = false
)
{
	glm::vec3 tmp = TransformToBone(
		glm::vec3(vec[0], vec[1], vec[2]),
		bones, binds,
		numBones, weights, boneIds,
		angleOnly
	);

	return Vector3(tmp.x, tmp.y, tmp.z);
}

void SkinTriangle(Triangle& tri, const std::vector<glm::mat4>& bones, const std::vector<glm::mat4>& binds)
{
	Vector3 vertexPositions[3] = {
					tri.p0,
					tri.p0 - tri.e1,
					tri.p0 + tri.e2
	};

	for (int vertIdx = 0; vertIdx < 3; vertIdx++) {
		vertexPositions[vertIdx] = TransformToBone(
			vertexPositions[vertIdx],
			bones, binds,
			tri.numBones[vertIdx], tri.weights[vertIdx], tri.boneIds[vertIdx]
		);

		tri.normals[vertIdx] = TransformToBone(
			tri.normals[vertIdx],
			bones, binds,
			tri.numBones[vertIdx], tri.weights[vertIdx], tri.boneIds[vertIdx],
			true
		);

		tri.tangents[vertIdx] = TransformToBone(
			tri.tangents[vertIdx],
			bones, binds,
			tri.numBones[vertIdx], tri.weights[vertIdx], tri.boneIds[vertIdx],
			true
		);
	}

	tri.p0 = vertexPositions[0];
	tri.e1 = vertexPositions[0] - vertexPositions[1];
	tri.e2 = vertexPositions[2] - vertexPositions[0];

	tri.ComputeNormalAndLoD();
}

void SkinTriangle(Triangle& tri, const glm::mat4 bone, glm::mat4 bind)
{
	std::vector<glm::mat4> bones{ bone };
	std::vector<glm::mat4> binds{ bind };
	SkinTriangle(tri, bones, binds);
}

Material ReadEntityMaterial(IMaterial* sourceMaterial, const std::string& materialPath)
{
	Material mat{};
	mat.path = materialPath;
	mat.maskedBlending = false;

	mat.baseTexPath = GetMaterialString(sourceMaterial, "$basetexture");
	mat.normalMapPath = GetMaterialString(sourceMaterial, "$bumpmap");
	mat.detailPath = GetMaterialString(sourceMaterial, "$detail");

	mat.baseTexture = ResourceCache::GetTexture(mat.baseTexPath, MISSING_TEXTURE);
	mat.normalMap = ResourceCache::GetTexture(mat.normalMapPath);
	mat.detail = ResourceCache::GetTexture(mat.detailPath);
	if (!mat.baseTexPath.empty()) mat.mrao = ResourceCache::GetTexture("vistrace/pbr/" + mat.baseTexPath + "_mrao");

	IMaterialVar* basetexturetransform = GetMaterialVar(sourceMaterial, "$basetexturetransform");
	if (basetexturetransform) {
		const VMatrix pMat = basetexturetransform->GetMatrixValue();
		mat.baseTexMat = pMat.To2x4();
	}

	IMaterialVar* bumptransform = GetMaterialVar(sourceMaterial, "$bumptransform");
	if (bumptransform) {
		const VMatrix pMat = bumptransform->GetMatrixValue();
		mat.normalMapMat = pMat.To2x4();
	}

	IMaterialVar* detailtexturetransform = GetMaterialVar(sourceMaterial, "$detailtexturetransform");
	if (detailtexturetransform) {
		const VMatrix pMat = detailtexturetransform->GetMatrixValue();
		mat.detailMat = pMat.To2x4();
	}

	IMaterialVar* detailscale = GetMaterialVar(sourceMaterial, "$detailscale");
	if (detailscale) {
		mat.detailScale = detailscale->GetFloatValue();
	}

	IMaterialVar* detailblendfactor = GetMaterialVar(sourceMaterial, "$detailblendfactor");
	if (detailblendfactor) {
		mat.detailBlendFactor = detailblendfactor->GetFloatValue();
	}

	IMaterialVar* detailblendmode = GetMaterialVar(sourceMaterial, "$detailblendmode");
	if (detailblendmode) {
		mat.detailBlendMode = static_cast<DetailBlendMode>(detailblendmode->GetIntValue());
	}

	IMaterialVar* detailtint = GetMaterialVar(sourceMaterial, "$detailtint");
	if (detailtint) {
		float values[3];
		detailtint->GetVecValue(values, 3);
		mat.detailTint = glm::vec3(values[0], values[1], values[2]);
	}

	IMaterialVar* detail_ambt = GetMaterialVar(sourceMaterial, "$detail_alpha_mask_base_texture");
	if (detail_ambt) {
		mat.detailAlphaMaskBaseTexture = detail_ambt->GetIntValue() != 0;
	}

	IMaterialVar* flags = GetMaterialVar(sourceMaterial, "$flags");
	if (flags) {
		mat.flags = static_cast<MaterialFlags>(flags->GetIntValue());
	}

	IMaterialVar* alphatestreference = GetMaterialVar(sourceMaterial, "$alphatestreference");
	if (alphatestreference) {
		mat.alphatestreference = alphatestreference->GetFloatValue();
	}

	return mat;
}

World::World(GarrysMod::Lua::ILuaBase* LUA, const std::string& mapName)
{
	std::string path = "maps/" + mapName + ".bsp";
	if (!FileSystem::Exists(path.c_str(), "GAME")) return;
	FileHandle_t file = FileSystem::Open(path.c_str(), "rb", "GAME");

	uint32_t filesize = FileSystem::Size(file);
	uint8_t* data = reinterpret_cast<uint8_t*>(malloc(filesize));
	if (data == nullptr) return;

	FileSystem::Read(data, filesize, file);
	FileSystem::Close(file);

	pMap = new BSPMap(data, filesize);
	free(data);

	if (!pMap->IsValid()) {
		delete pMap;
		pMap = nullptr;
		return;
	}

	triangles = std::vector<Triangle>();
	entities = std::vector<Entity>();
	materials = std::vector<Material>();

	if (
		ResourceCache::GetTexture(MISSING_TEXTURE) == nullptr ||
		ResourceCache::GetModel(MISSING_MODEL) == nullptr
	) {
		delete pMap;
		pMap = nullptr;
		return;
	}

	ResourceCache::GetTexture(WATER_BASE_TEXTURE);

	const glm::vec3* vertices = reinterpret_cast<const glm::vec3*>(pMap->GetVertices());
	const glm::vec3* normals = reinterpret_cast<const glm::vec3*>(pMap->GetNormals());
	const glm::vec3* tangents = reinterpret_cast<const glm::vec3*>(pMap->GetTangents());
	const glm::vec3* binormals = reinterpret_cast<const glm::vec3*>(pMap->GetBinormals());
	const glm::vec2* uvs = reinterpret_cast<const glm::vec2*>(pMap->GetUVs());
	const float* alphas = pMap->GetAlphas();
	const int16_t* textures = pMap->GetTriTextures();

	Entity world{};
	world.rawEntity = nullptr; // replace with world ent ptr
	world.id = 0;
	world.colour = glm::vec4(1, 1, 1, 1);
	world.materials = std::vector<size_t>();

	LUA->PushSpecial(SPECIAL_GLOB); // _G
	auto submatIds = std::unordered_map<std::string, size_t>();

	triangles.reserve(pMap->GetNumTris());
	for (size_t triIdx = 0; triIdx < pMap->GetNumTris(); triIdx++) {
		size_t vi0 = triIdx * 3;
		size_t vi1 = vi0 + 1, vi2 = vi0 + 2;

		// Load texture
		BSPTexture tex;
		try {
			tex = pMap->GetTexture(textures[triIdx]);
		} catch (std::out_of_range e) {
			delete pMap;
			pMap = nullptr;
			LUA->ThrowError(e.what());
		}

		const std::string strPath = tex.path;
		if (materialIds.find(strPath) == materialIds.end()) {
			LUA->GetField(-1, "Material"); // _G Material
			LUA->PushString(tex.path); // _G Material string
			LUA->Call(1, 1); // _G IMaterial
			if (!LUA->IsType(-1, Type::Material)) {
				delete pMap;
				pMap = nullptr;
				LUA->ThrowError("Invalid material on world");
			}

			IMaterial* sourceMaterial = LUA->GetUserType<IMaterial>(-1, Type::Material);
			Material mat{};
			mat.path = tex.path;
			mat.maskedBlending = false;
			
			const char* shaderName = sourceMaterial->GetShaderName();

			if (shaderName == nullptr || strncmp(shaderName, "Water", 5) != 0) {
				IMaterialVar* maskedblending = GetMaterialVar(sourceMaterial, "$maskedblending");
				if (maskedblending) {
					mat.maskedBlending = maskedblending->GetIntValue() != 0;
				}

				mat.baseTexPath = GetMaterialString(sourceMaterial, "$basetexture");
				mat.normalMapPath = GetMaterialString(sourceMaterial, "$bumpmap");

				mat.baseTexPath2 = GetMaterialString(sourceMaterial, "$basetexture2");
				mat.normalMapPath2 = GetMaterialString(sourceMaterial, "$bumpmap2");

				mat.blendTexPath = GetMaterialString(sourceMaterial, "$blendmodulatetexture");

				mat.detailPath = GetMaterialString(sourceMaterial, "$detail");

				mat.baseTexture = ResourceCache::GetTexture(mat.baseTexPath, MISSING_TEXTURE);
				mat.normalMap = ResourceCache::GetTexture(mat.normalMapPath);
				if (!mat.baseTexPath.empty()) mat.mrao = ResourceCache::GetTexture("vistrace/pbr/" + mat.baseTexPath + "_mrao");

				mat.baseTexture2 = ResourceCache::GetTexture(mat.baseTexPath2);
				mat.normalMap2 = ResourceCache::GetTexture(mat.normalMapPath2);
				if (!mat.baseTexPath2.empty()) mat.mrao2 = ResourceCache::GetTexture("vistrace/pbr/" + mat.baseTexPath2 + "_mrao");

				mat.blendTexture = ResourceCache::GetTexture(mat.blendTexPath);
				mat.detail = ResourceCache::GetTexture(mat.detailPath);

				IMaterialVar* basetexturetransform = GetMaterialVar(sourceMaterial, "$basetexturetransform");
				if (basetexturetransform) {
					const VMatrix pMat = basetexturetransform->GetMatrixValue();
					mat.baseTexMat = pMat.To2x4();
				}

				IMaterialVar* bumptransform = GetMaterialVar(sourceMaterial, "$bumptransform");
				if (bumptransform) {
					const VMatrix pMat = bumptransform->GetMatrixValue();
					mat.normalMapMat = pMat.To2x4();
				}

				IMaterialVar* basetexturetransform2 = GetMaterialVar(sourceMaterial, "$basetexturetransform2");
				if (basetexturetransform2) {
					const VMatrix pMat = basetexturetransform2->GetMatrixValue();
					mat.baseTexMat2 = pMat.To2x4();
				}

				IMaterialVar* bumptransform2 = GetMaterialVar(sourceMaterial, "$bumptransform2");
				if (bumptransform2) {
					const VMatrix pMat = bumptransform2->GetMatrixValue();
					mat.normalMapMat2 = pMat.To2x4();
				}

				IMaterialVar* blendmasktransform = GetMaterialVar(sourceMaterial, "$blendmasktransform");
				if (blendmasktransform) {
					const VMatrix pMat = blendmasktransform->GetMatrixValue();
					mat.blendTexMat = pMat.To2x4();
				}

				IMaterialVar* detailtexturetransform = GetMaterialVar(sourceMaterial, "$detailtexturetransform");
				if (detailtexturetransform) {
					const VMatrix pMat = detailtexturetransform->GetMatrixValue();
					mat.detailMat = pMat.To2x4();
				}

				IMaterialVar* detailscale = GetMaterialVar(sourceMaterial, "$detailscale");
				if (detailscale) {
					mat.detailScale = detailscale->GetFloatValue();
				}

				IMaterialVar* detailblendfactor = GetMaterialVar(sourceMaterial, "$detailblendfactor");
				if (detailblendfactor) {
					mat.detailBlendFactor = detailblendfactor->GetFloatValue();
				}

				IMaterialVar* detailblendmode = GetMaterialVar(sourceMaterial, "$detailblendmode");
				if (detailblendmode) {
					mat.detailBlendMode = static_cast<DetailBlendMode>(detailblendmode->GetIntValue());
				}

				IMaterialVar* alphatestreference = GetMaterialVar(sourceMaterial, "$alphatestreference");
				if (alphatestreference) {
					mat.alphatestreference = alphatestreference->GetFloatValue();
				}

				IMaterialVar* detailtint = GetMaterialVar(sourceMaterial, "$detailtint");
				if (detailtint) {
					float values[3];
					detailtint->GetVecValue(values, 3);
					mat.detailTint = glm::vec3(values[0], values[1], values[2]);
				}

				IMaterialVar* detail_ambt = GetMaterialVar(sourceMaterial, "$detail_alpha_mask_base_texture");
				if (detail_ambt) {
					mat.detailAlphaMaskBaseTexture = detail_ambt->GetIntValue() != 0;
				}
			} else {
				mat.water = true;
				mat.normalMapPath = GetMaterialString(sourceMaterial, "$normalmap");

				IMaterialVar* fogcolor = GetMaterialVar(sourceMaterial, "$fogcolor");
				if (fogcolor) {
					float values[3];
					fogcolor->GetVecValue(values, 3);

					mat.colour = glm::vec4(values[0], values[1], values[2], 1);
				}

				// Not sure if any gmod materials will even implement water base textures
				// Or if it's even available in gmod's engine version, but here just in case
				mat.baseTexPath = GetMaterialString(sourceMaterial, "$basetexture");

				mat.baseTexture = ResourceCache::GetTexture(mat.baseTexPath, WATER_BASE_TEXTURE);
				if (mat.baseTexture == nullptr) mat.baseTexture = ResourceCache::GetTexture(MISSING_TEXTURE);
				mat.normalMap = ResourceCache::GetTexture(mat.normalMapPath);
			}

			IMaterialVar* flags = GetMaterialVar(sourceMaterial, "$flags");
			if (flags) {
				mat.flags = static_cast<MaterialFlags>(flags->GetIntValue());
			}

			LUA->Pop(); // _G

			mat.surfFlags = tex.flags;

			submatIds.emplace(strPath, world.materials.size());
			world.materials.push_back(materials.size());
			materialIds.emplace(strPath, materials.size());
			materials.push_back(mat);
		}

		// Construct bvh tri
		Triangle tri(
			Vector3{ vertices[vi0].x, vertices[vi0].y, vertices[vi0].z },
			Vector3{ vertices[vi1].x, vertices[vi1].y, vertices[vi1].z },
			Vector3{ vertices[vi2].x, vertices[vi2].y, vertices[vi2].z },
			world.materials[submatIds[strPath]],

			// Backface cull on the world to prevent z fighting on 2 sided water surfaces
			// (given you shouldnt be refracting through any other brushes this should be fine)
			true
		);

		memcpy(tri.normals, normals + vi0, sizeof(glm::vec3) * 3);
		memcpy(tri.tangents, tangents + vi0, sizeof(glm::vec3) * 3);
		memcpy(tri.uvs, uvs + vi0, sizeof(glm::vec2) * 3);
		memcpy(tri.alphas, alphas + vi0, sizeof(float) * 3);

		triangles.push_back(tri);
	}

	entities.push_back(world);

	// Add static props
	for (int i = 0; i < pMap->GetNumStaticProps(); i++) {
		Entity entData{};
		entData.id = world.id;
		entData.rawEntity = world.rawEntity;
		entData.colour = world.colour;
		entData.materials = std::vector<size_t>();

		BSPStaticProp prop;
		try {
			prop = pMap->GetStaticProp(i);
		} catch (std::out_of_range e) {
			LUA->ThrowError(e.what());
		} catch (std::runtime_error e) {
			LUA->ThrowError(e.what()); // This means we didn't check the map was valid first
		}

		glm::mat4 bone = glm::translate(glm::identity<glm::mat4>(), glm::vec3(prop.pos.x, prop.pos.y, prop.pos.z));
		glm::mat4 angle = glm::eulerAngleZYX(glm::radians(prop.ang.y), glm::radians(prop.ang.x), glm::radians(prop.ang.z));
		bone *= angle;

		// Cache model
		const Model* pModel = ResourceCache::GetModel(prop.model, MISSING_MODEL);

		// Cache bind pose
		glm::mat4 bind = pModel->GetBindMatrix(0);

		// Get materials
		entData.materials.reserve(pModel->GetNumMaterials());
		for (int materialId = 0; materialId < pModel->GetNumMaterials(); materialId++) {
			std::string materialPath = pModel->GetMaterial(materialId);

			if (materialIds.find(materialPath) == materialIds.end()) {
				LUA->GetField(-1, "Material");
				LUA->PushString(materialPath.c_str());
				LUA->Call(1, 1);
				if (!LUA->IsType(-1, Type::Material)) LUA->ThrowError("Invalid material on entity");

				// Grab the source material
				IMaterial* sourceMaterial = LUA->GetUserType<IMaterial>(-1, Type::Material);

				// Read props
				Material mat = ReadEntityMaterial(sourceMaterial, materialPath);

				LUA->Pop(); // _G

				materialIds.emplace(materialPath, materials.size());
				materials.push_back(mat);
			}

			entData.materials.push_back(materialIds[materialPath]);
		}

		for (size_t bodygroupIdx = 0; bodygroupIdx < pModel->GetNumBodyGroups(); bodygroupIdx++) {
			const Mesh* pMesh = pModel->GetMesh(bodygroupIdx, 0);

			size_t triStart = triangles.size();
			triangles.insert(
				triangles.end(),
				pMesh->GetTriangles(),
				pMesh->GetTriangles() + pMesh->GetNumTriangles()
			);

			for (int triIdx = triStart; triIdx < pMesh->GetNumTriangles() + triStart; triIdx++) {
				Triangle& tri = triangles[triIdx];

				tri.entIdx = entities.size();
				tri.material = entData.materials[pModel->GetMaterialIdx(prop.skin, tri.material)];

				SkinTriangle(tri, bone, bind);
			}
		}

		entities.push_back(entData);
	}

	LUA->Pop(); // Pop _G
}

World::~World()
{
	if (pMap != nullptr) delete pMap;
}

bool World::IsValid() const
{
	return pMap != nullptr;
}

AccelStruct::AccelStruct()
{
	mpIntersector = nullptr;
	mpTraverser = nullptr;
	mAccelBuilt = false;

	mTriangles = std::vector<Triangle>();

	mEntities = std::vector<Entity>();

	mMaterialIds = std::unordered_map<std::string, size_t>();
	mMaterials = std::vector<Material>();
}

AccelStruct::~AccelStruct()
{
	if (mAccelBuilt) {
		delete mpIntersector;
		delete mpTraverser;
	}
}

void AccelStruct::PopulateAccel(ILuaBase* LUA, const World* pWorld)
{
	mpWorld = pWorld;

	// Delete accel
	if (mAccelBuilt) {
		mAccelBuilt = false;
		delete mpIntersector;
		delete mpTraverser;
	}

	// Redefine containers
	mTriangles.clear();

	mEntities.clear();

	mMaterialIds.clear();
	mMaterials.clear();

	if (mpWorld != nullptr) {
		mTriangles = mpWorld->triangles;
		mEntities = mpWorld->entities;
		mMaterials = mpWorld->materials;
	} else if (
		ResourceCache::GetTexture(MISSING_TEXTURE) == nullptr ||
		ResourceCache::GetModel(MISSING_MODEL) == nullptr
	) {
		LUA->ThrowError("Failed to read missing texture or error model");
	}

	LUA->PushSpecial(SPECIAL_GLOB);
	LUA->Insert(1);

	// Iterate over entities
	size_t numEntities = LUA->ObjLen();
	mEntities.reserve(mEntities.size() + numEntities);
	for (size_t entIndex = 1; entIndex <= numEntities; entIndex++) {
		Entity entData{};

		// Get entity
		LUA->PushNumber(entIndex);
		LUA->GetTable(-2);
		if (!LUA->IsType(-1, Type::Entity)) LUA->ThrowError("Build list must only contain entities");

		// Make sure entity is valid
		LUA->GetField(-1, "IsValid");
		LUA->Push(-2);
		LUA->Call(1, 1);
		if (!LUA->GetBool()) LUA->ThrowError("Attempted to build accel from an invalid entity");
		LUA->Pop(); // Pop the bool

		// If the entity defines a custom draw function, continue
		LUA->GetField(-1, "Draw");
		if (LUA->IsType(-1, Type::Function)) {
			LUA->Pop(2); // Pop function and entity
			continue;
		}
		LUA->Pop(); // Pop nil

		// Get entity id
		{
			LUA->GetField(-1, "EntIndex");
			LUA->Push(-2);
			LUA->Call(1, 1);
			double entId = LUA->GetNumber(); // Get as a double so after we check it's positive a static cast to unsigned int wont underflow rather than using int
			LUA->Pop();

			if (entId < 0.0) LUA->ThrowError("Entity ID is less than 0");
			entData.id = entId;
		}

		// Get entity colour
		LUA->GetField(-1, "GetColor");
		LUA->Push(-2);
		LUA->Call(1, 1);

		LUA->GetField(-1, "r");
		entData.colour[0] = LUA->GetNumber() / 255.f;
		LUA->Pop();

		LUA->GetField(-1, "g");
		entData.colour[1] = LUA->GetNumber() / 255.f;
		LUA->Pop();

		LUA->GetField(-1, "b");
		entData.colour[2] = LUA->GetNumber() / 255.f;
		LUA->Pop();

		LUA->GetField(-1, "a");
		entData.colour[3] = LUA->GetNumber() / 255.f;
		LUA->Pop(2);

		// Cache model
		LUA->GetField(-1, "GetModel");
		LUA->Push(-2);
		LUA->Call(1, 1);

		const Model* pModel = LUA->IsType(-1, Type::String) ?
			ResourceCache::GetModel(LUA->GetString(), MISSING_MODEL) :
			ResourceCache::GetModel(MISSING_MODEL);
		LUA->Pop();

		// Cache bone transforms
		// Make sure the bone transforms are updated and the bones themselves are valid
		LUA->GetField(-1, "SetupBones");
		LUA->Push(-2);
		LUA->Call(1, 0);

		// Get number of bones and make sure the value is valid
		LUA->GetField(-1, "GetBoneCount");
		LUA->Push(-2);
		LUA->Call(1, 1);
		int numBones = LUA->GetNumber();
		LUA->Pop();

		if (numBones < 1) LUA->ThrowError("Entity has invalid bones");
		if (numBones != pModel->GetNumBones()) LUA->ThrowError("Entity bones don't match model");

		// For each bone, cache the transform
		auto bones = std::vector<glm::mat4>(numBones);
		auto binds = std::vector<glm::mat4>(numBones);
		for (int boneIndex = 0; boneIndex < numBones; boneIndex++) {
			LUA->GetField(-1, "GetBoneMatrix");
			LUA->Push(-2);
			LUA->PushNumber(boneIndex);
			LUA->Call(2, 1);

			glm::mat4 transform = glm::identity<glm::mat4>();
			if (LUA->IsType(-1, Type::Matrix)) {
				const VMatrix* pMat = LUA->GetUserType<VMatrix>(-1, Type::Matrix);
				transform = pMat->To4x4();
			}
			LUA->Pop();

			bones[boneIndex] = transform;
			binds[boneIndex] = pModel->GetBindMatrix(boneIndex);
		}

		// Get materials
		entData.materials.reserve(pModel->GetNumMaterials());
		for (int materialId = 0; materialId < pModel->GetNumMaterials(); materialId++) {
			std::string materialPath = "";
			LUA->GetField(-1, "GetMaterial");
			LUA->Push(-2);
			LUA->Call(1, 1);
			if (LUA->IsType(-1, Type::String)) materialPath = LUA->GetString();
			LUA->Pop();

			if (materialPath.empty()) {
				LUA->GetField(-1, "GetSubMaterial");
				LUA->Push(-2);
				LUA->PushNumber(materialId);
				LUA->Call(2, 1);
				if (LUA->IsType(-1, Type::String)) materialPath = LUA->GetString();
				LUA->Pop();

				if (materialPath.empty()) {
					materialPath = pModel->GetMaterial(materialId);
				}
			}

			if (mMaterialIds.find(materialPath) == mMaterialIds.end()) {
				LUA->GetField(1, "Material");
				LUA->PushString(materialPath.c_str());
				LUA->Call(1, 1);
				if (!LUA->IsType(-1, Type::Material)) LUA->ThrowError("Invalid material on entity");

				// Grab the source material
				IMaterial* sourceMaterial = LUA->GetUserType<IMaterial>(-1, Type::Material);

				// Read props
				Material mat = ReadEntityMaterial(sourceMaterial, materialPath);

				// Pop the material
				LUA->Pop();

				mMaterialIds.emplace(materialPath, mMaterials.size());
				mMaterials.push_back(mat);
			}

			entData.materials.push_back(mMaterialIds[materialPath]);
		}

		for (size_t bodygroupIdx = 0; bodygroupIdx < pModel->GetNumBodyGroups(); bodygroupIdx++) {
			// Get bodygroup value
			LUA->GetField(-1, "GetBodygroup");
			LUA->Push(-2);
			LUA->PushNumber(bodygroupIdx);
			LUA->Call(2, 1);

			int bodygroupVal = LUA->GetNumber();
			LUA->Pop();

			// Get skin
			LUA->GetField(-1, "GetSkin");
			LUA->Push(-2);
			LUA->Call(1, 1);

			int skin = LUA->GetNumber();
			LUA->Pop();

			const Mesh* pMesh = pModel->GetMesh(bodygroupIdx, bodygroupVal);

			size_t triStart = mTriangles.size();
			mTriangles.insert(
				mTriangles.end(),
				pMesh->GetTriangles(),
				pMesh->GetTriangles() + pMesh->GetNumTriangles()
			);

			for (int triIdx = triStart; triIdx < pMesh->GetNumTriangles() + triStart; triIdx++) {
				Triangle& tri = mTriangles[triIdx];

				tri.entIdx = mEntities.size();
				tri.material = entData.materials[pModel->GetMaterialIdx(skin, tri.material)];

				SkinTriangle(tri, bones, binds);
			}
		}

		// Save the entity's pointer for hit verification later
		entData.rawEntity = LUA->GetUserType<CBaseEntity>(-1, Type::Entity);
		LUA->Pop(); // Pop entity

		mEntities.push_back(entData);
	}

	LUA->Pop(); // Pop entity table

	// Build BVH
	mAccel = BVH();
	bvh::LocallyOrderedClusteringBuilder<BVH, uint32_t> builder(mAccel);
	auto [bboxes, centers] = bvh::compute_bounding_boxes_and_centers(mTriangles.data(), mTriangles.size());
	auto global_bbox = bvh::compute_bounding_boxes_union(bboxes.get(), mTriangles.size());
	builder.build(global_bbox, bboxes.get(), centers.get(), mTriangles.size());

	bvh::LeafCollapser collapser(mAccel);
	collapser.collapse();

	mpIntersector = new Intersector(mAccel, mTriangles.data());
	mpTraverser = new Traverser(mAccel);

	mAccelBuilt = true;
}

int AccelStruct::Traverse(ILuaBase* LUA)
{
	if (!mAccelBuilt) LUA->ThrowError("Unable to perform traversal, acceleration structure invalid (use AccelStruct:Rebuild to rebuild it)");
	int numArgs = LUA->Top();

	// Parse arguments
	LUA->CheckType(2, Type::Vector);
	LUA->CheckType(3, Type::Vector);

	Vector origin = LUA->GetVector(2);
	Vector direction = LUA->GetVector(3);

	float tMin = 0.f;
	if (numArgs > 3 && !LUA->IsType(4, Type::Nil)) tMin = static_cast<float>(LUA->CheckNumber(4));

	float tMax = FLT_MAX;
	if (numArgs > 4 && !LUA->IsType(5, Type::Nil)) tMax = static_cast<float>(LUA->CheckNumber(5));

	float coneWidth = -1; // Negatives will only sample mip level 0
	if (numArgs > 5 && !LUA->IsType(6, Type::Nil)) coneWidth = static_cast<float>(LUA->CheckNumber(6));

	float coneAngle = -1; // < 0 will only sample mip level 0
	if (numArgs > 6 && !LUA->IsType(7, Type::Nil)) coneAngle = static_cast<float>(LUA->CheckNumber(7));

	if (coneWidth >= 0 && coneAngle <= 0.f) LUA->ThrowError("Valid cone width but invalid cone angle passed");
	if (coneWidth < 0 && coneAngle > 0.f) LUA->ThrowError("Valid cone angle but invalid cone width passed");

	if (tMin < 0.f) LUA->ArgError(4, "tMin cannot be less than 0");
	if (tMax <= tMin) LUA->ArgError(5, "tMax must be greater than tMin");

	LUA->Pop(LUA->Top()); // Clear the stack of any items

	Ray ray(
		Vector3(origin.x, origin.y, origin.z),
		Vector3(direction.x, direction.y, direction.z),
		this,
		tMin, tMax
	);

	// Perform BVH traversal for mesh hit
	auto hit = mpTraverser->traverse(ray, *mpIntersector);
	if (hit) {
		auto intersection = hit->intersection;
		const Triangle& tri = mTriangles[hit->primitive_index];
		const Entity& ent = mEntities[tri.entIdx];
		const Material& mat = mMaterials[tri.material];

		TraceResult* pRes = new TraceResult(
			glm::normalize(glm::vec3(direction.x, direction.y, direction.z)), hit->distance(),
			coneWidth, coneAngle,
			tri,
			glm::vec2(hit->intersection.u, hit->intersection.v),
			ent, mat
		);

		LUA->PushUserType_Value(pRes, TraceResult::id);
		return 1;
	}

	return 0;
}

const Material& AccelStruct::GetMaterial(const size_t i) const
{
	return mMaterials[i];
}

#include <stdexcept>

#include "GMFS.h"

#include "AccelStruct.h"
#include "Utils.h"

#include "TraceResult.h"

#define MISSING_TEXTURE "debug/debugempty"
#define WATER_BASE_TEXTURE "models/debug/debugwhite"

using namespace GarrysMod::Lua;

void normalise(Vector3& v)
{
	float length = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	v[0] /= length;
	v[1] /= length;
	v[2] /= length;
}

VTFTexture* CacheTexture(
	const std::string& path,
	std::unordered_map<std::string, VTFTexture*>& cache,
	VTFTexture* fallback = nullptr
)
{
	if (cache.find(path) != cache.end()) return cache[path];

	if (!path.empty()) {
		VTFTexture* pTexture;
		if (readTexture(path, &pTexture)) {
			cache.emplace(path, pTexture);
			return pTexture;
		};
	}

	return fallback;
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
	triangleData = std::vector<TriangleData>();

	entities = std::vector<Entity>();

	textureCache = std::unordered_map<std::string, VTFTexture*>();

	materials = std::vector<Material>();

	{
		VTFTexture* pTexture;
		if (!readTexture(MISSING_TEXTURE, &pTexture)) {
			delete pMap;
			pMap = nullptr;
			return;
		}
		textureCache.emplace(MISSING_TEXTURE, pTexture);

		if (readTexture(WATER_BASE_TEXTURE, &pTexture)) {
			textureCache.emplace(WATER_BASE_TEXTURE, pTexture);
		}
	}

	const glm::vec3* vertices = reinterpret_cast<const glm::vec3*>(pMap->GetVertices());
	const glm::vec3* normals = reinterpret_cast<const glm::vec3*>(pMap->GetNormals());
	const glm::vec3* tangents = reinterpret_cast<const glm::vec3*>(pMap->GetTangents());
	const glm::vec3* binormals = reinterpret_cast<const glm::vec3*>(pMap->GetBinormals());
	const glm::vec2* uvs = reinterpret_cast<const glm::vec2*>(pMap->GetUVs());
	const float* alphas = pMap->GetAlphas();
	const int16_t* textures = pMap->GetTriTextures();

	Entity ent{};
	ent.rawEntity = nullptr; // replace with world ent ptr
	ent.id = 0;
	ent.colour = glm::vec4(1, 1, 1, 1);
	ent.materials = std::vector<size_t>();

	LUA->PushSpecial(SPECIAL_GLOB);
	auto submatIds = std::unordered_map<std::string, size_t>();
	for (size_t triIdx = 0; triIdx < pMap->GetNumTris(); triIdx++) {
		size_t vi0 = triIdx * 3;
		size_t vi1 = vi0 + 1, vi2 = vi0 + 2;

		// Construct bvh tri
		triangles.push_back(Triangle{
			Vector3{ vertices[vi0].x, vertices[vi0].y, vertices[vi0].z },
			Vector3{ vertices[vi1].x, vertices[vi1].y, vertices[vi1].z },
			Vector3{ vertices[vi2].x, vertices[vi2].y, vertices[vi2].z },

			// Backface cull on the world to prevent z fighting on 2 sided water surfaces
			// (given you shouldnt be refracting through any other brushes this should be fine)
			true
		});

		// Construct tri data
		TriangleData triData{};
		triData.ignoreNormalMap = false;
		memcpy(triData.normals, normals + vi0, sizeof(glm::vec3) * 3);
		memcpy(triData.tangents, tangents + vi0, sizeof(glm::vec3) * 3);
		memcpy(triData.binormals, binormals + vi0, sizeof(glm::vec3) * 3);
		memcpy(triData.uvs, uvs + vi0, sizeof(glm::vec2) * 3);
		memcpy(triData.alphas, alphas + vi0, sizeof(float) * 3);
		triData.entIdx = 0;

		// Load texture
		BSPTexture tex;
		try {
			tex = pMap->GetTexture(textures[triIdx]);
		} catch (std::runtime_error e) {
			delete pMap;
			pMap = nullptr;
			for (auto& [key, element] : textureCache) {
				delete element;
			}
			LUA->ThrowError(e.what());
		}

		const std::string strPath = tex.path;
		if (materialIds.find(strPath) == materialIds.end()) {
			LUA->GetField(-1, "Material");
			LUA->PushString(tex.path);
			LUA->Call(1, 1);
			if (!LUA->IsType(-1, Type::Material)) LUA->ThrowError("Invalid material on world");

			Material mat{};
			mat.maskedBlending = false;

			// Note, GetShader doesnt work on Linux srcds according to the wiki
			// If I made a server binary and water didnt render correctly, this is why
			LUA->GetField(-1, "GetShader");
			LUA->Push(-2);
			LUA->Call(1, 1);
			const char* shaderName = LUA->GetString();
			LUA->Pop();

			if (shaderName == nullptr || strncmp(shaderName, "Water", 5) != 0) {
				LUA->GetField(-1, "GetInt");
				LUA->Push(-2);
				LUA->PushString("$maskedblending");
				LUA->Call(2, 1);
				if (LUA->IsType(-1, Type::Number)) mat.maskedBlending = LUA->GetNumber() != 0;
				LUA->Pop();

				std::string baseTexture = getMaterialString(LUA, "$basetexture");
				std::string normalMap = getMaterialString(LUA, "$bumpmap");

				std::string baseTexture2 = getMaterialString(LUA, "$basetexture2");
				std::string normalMap2 = getMaterialString(LUA, "$bumpmap2");
				std::string blendTexture = getMaterialString(LUA, "$blendmodulatetexture");

				mat.baseTexture = CacheTexture(baseTexture, textureCache, textureCache[MISSING_TEXTURE]);
				mat.normalMap = CacheTexture(normalMap, textureCache);

				mat.baseTexture2 = CacheTexture(baseTexture2, textureCache);
				mat.normalMap2 = CacheTexture(normalMap2, textureCache);
				mat.blendTexture = CacheTexture(blendTexture, textureCache);
			} else {
				mat.water = true;
				std::string normalMap = getMaterialString(LUA, "$normalmap");

				LUA->GetField(-1, "GetVector");
				LUA->Push(-2);
				LUA->PushString("$fogcolor");
				LUA->Call(2, 1);
				if (LUA->IsType(-1, Type::Vector)) {
					Vector v = LUA->GetVector();
					mat.colour = glm::vec4(v.x, v.y, v.z, 1);
				}
				LUA->Pop();

				// Not sure if any gmod materials will even implement water base textures
				// Or if it's even available in gmod's engine version, but here just in case
				std::string baseTexture = getMaterialString(LUA, "$basetexture");

				mat.baseTexture = CacheTexture(
					baseTexture, textureCache,
					(textureCache.find(WATER_BASE_TEXTURE) == textureCache.end()) ? textureCache[MISSING_TEXTURE] : textureCache[WATER_BASE_TEXTURE]
				);
				mat.normalMap = CacheTexture(normalMap, textureCache);
			}

			LUA->GetField(-1, "GetInt");
			LUA->Push(-2);
			LUA->PushString("$flags");
			LUA->Call(2, 1);
			if (LUA->IsType(-1, Type::Number)) mat.flags = static_cast<MaterialFlags>(LUA->GetNumber());
			LUA->Pop();

			LUA->Pop();

			mat.surfFlags = tex.flags;

			submatIds.emplace(strPath, ent.materials.size());
			ent.materials.push_back(materials.size());
			materialIds.emplace(strPath, materials.size());
			materials.push_back(mat);
		}
		triData.submatIdx = submatIds[strPath];

		triangleData.push_back(triData);
	}
	LUA->Pop();

	entities.push_back(ent);
}

World::~World()
{
	if (pMap != nullptr) delete pMap;
	for (auto& [key, element] : textureCache) {
		delete element;
	}
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
	mTriangleData = std::vector<TriangleData>();

	mEntities = std::vector<Entity>();

	mTextureCache = std::unordered_map<std::string, VTFTexture*>();

	mMaterialIds = std::unordered_map<std::string, size_t>();
	mMaterials = std::vector<Material>();
}

AccelStruct::~AccelStruct()
{
	if (mAccelBuilt) {
		delete mpIntersector;
		delete mpTraverser;
	}
	for (auto& [key, element] : mTextureCache) {
		delete element;
	}
}

void AccelStruct::PopulateAccel(ILuaBase* LUA, const World* pWorld)
{
	// Delete accel
	if (mAccelBuilt) {
		mAccelBuilt = false;
		delete mpIntersector;
		delete mpTraverser;
	}

	// Redefine containers
	for (auto& [key, element] : mTextureCache) {
		delete element;
	}

	mTriangles.erase(mTriangles.begin(), mTriangles.end());
	mTriangleData.erase(mTriangleData.begin(), mTriangleData.end());

	mEntities.erase(mEntities.begin(), mEntities.end());

	mTextureCache.erase(mTextureCache.begin(), mTextureCache.end());

	mMaterialIds.erase(mMaterialIds.begin(), mMaterialIds.end());
	mMaterials.erase(mMaterials.begin(), mMaterials.end());

	if (pWorld != nullptr) {
		mTriangles = pWorld->triangles;
		mTriangleData = pWorld->triangleData;

		mEntities = pWorld->entities;

		for (auto& [path, texture] : pWorld->textureCache) { // Copy images into accelstruct
			mTextureCache.emplace(path, new VTFTexture(*texture));
		}

		mMaterials = pWorld->materials;
	}

	// Iterate over entities
	size_t numEntities = LUA->ObjLen();
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

		// For each bone, cache the transform
		auto bones = std::vector<glm::mat4>(numBones);
		for (int boneIndex = 0; boneIndex < numBones; boneIndex++) {
			LUA->GetField(-1, "GetBoneMatrix");
			LUA->Push(-2);
			LUA->PushNumber(boneIndex);
			LUA->Call(2, 1);

			glm::mat4 transform = glm::identity<glm::mat4>();
			if (LUA->IsType(-1, Type::Matrix)) {
				for (unsigned char row = 0; row < 4; row++) {
					for (unsigned char col = 0; col < 4; col++) {
						LUA->GetField(-1, "GetField");
						LUA->Push(-2);
						LUA->PushNumber(row + 1);
						LUA->PushNumber(col + 1);
						LUA->Call(3, 1);

						transform[col][row] = LUA->GetNumber();
						LUA->Pop();
					}
				}
			}
			LUA->Pop();

			bones[boneIndex] = transform;
		}

		// Iterate over meshes
		LUA->PushSpecial(SPECIAL_GLOB);
		LUA->GetField(-1, "util");
		LUA->GetField(-1, "GetModelMeshes");
		LUA->GetField(-4, "GetModel");
		LUA->Push(-5);
		LUA->Call(1, 1);
		LUA->Call(1, 2);

		// Make sure both return values are present and valid
		if (!LUA->IsType(-2, Type::Table)) {
			LUA->Pop(2); // Pop the 2 nils

			LUA->GetField(-1, "GetModelMeshes");
			LUA->PushString("models/error.mdl");
			LUA->Call(1, 2);

			if (!LUA->IsType(-2, Type::Table)) LUA->ThrowError("Entity model invalid and error model not available"); // This would only ever happen if the user's game is corrupt
		}
		if (!LUA->IsType(-1, Type::Table)) LUA->ThrowError("Entity bind pose not returned (this likely means you're running an older version of GMod)");

		// Cache bind pose
		auto bindBones = std::vector<glm::mat4>(numBones);
		for (int boneIndex = 0; boneIndex < numBones; boneIndex++) {
			LUA->PushNumber(boneIndex);
			LUA->GetTable(-2);
			LUA->GetField(-1, "matrix");

			glm::mat4 transform = glm::identity<glm::mat4>();
			if (LUA->IsType(-1, Type::Matrix)) {
				for (unsigned char row = 0; row < 4; row++) {
					for (unsigned char col = 0; col < 4; col++) {
						LUA->GetField(-1, "GetField");
						LUA->Push(-2);
						LUA->PushNumber(row + 1);
						LUA->PushNumber(col + 1);
						LUA->Call(3, 1);

						transform[col][row] = LUA->GetNumber();
						LUA->Pop();
					}
				}
			}
			LUA->Pop(2);

			bindBones[boneIndex] = transform;
		}
		LUA->Pop();

		size_t numSubmeshes = LUA->ObjLen();
		for (size_t meshIndex = 1; meshIndex <= numSubmeshes; meshIndex++) {
			// Get mesh
			LUA->PushNumber(meshIndex);
			LUA->GetTable(-2);

			// Iterate over tris
			LUA->GetField(-1, "triangles");
			if (!LUA->IsType(-1, Type::Table)) LUA->ThrowError("Vertices tables must contain MeshVertex tables");
			size_t numVerts = LUA->ObjLen();
			if (numVerts % 3U != 0U) LUA->ThrowError("Number of vertices is not a multiple of 3");

			glm::vec3 tri[3];
			TriangleData triData{};
			triData.entIdx = mEntities.size();
			triData.submatIdx = entData.materials.size();
			triData.alphas[0] = triData.alphas[1] = triData.alphas[2] = 1.f;
			for (size_t vertIndex = 0; vertIndex < numVerts; vertIndex++) {
				// Get vertex
				LUA->PushNumber(vertIndex + 1U);
				LUA->GetTable(-2);

				// Get weights
				LUA->GetField(-1, "weights");
				auto weights = std::vector<std::pair<size_t, float>>();
				{
					size_t numWeights = LUA->ObjLen();
					for (size_t weightIndex = 1U; weightIndex <= numWeights; weightIndex++) {
						LUA->PushNumber(weightIndex);
						LUA->GetTable(-2);
						LUA->GetField(-1, "bone");
						LUA->GetField(-2, "weight");
						weights.emplace_back(LUA->GetNumber(-2), LUA->GetNumber());
						LUA->Pop(3);
					}
				}
				LUA->Pop();

				// Get and transform position
				LUA->GetField(-1, "pos");

				size_t triIndex = vertIndex % 3U;
				tri[triIndex] = transformToBone(LUA->GetVector(), bones, bindBones, weights);

				LUA->Pop();

				// Get and transform normal, tangent, and binormal
				LUA->GetField(-1, "normal");
				if (LUA->IsType(-1, Type::Vector)) {
					triData.normals[triIndex] = transformToBone(LUA->GetVector(), bones, bindBones, weights, true);
				} else {
					triData.normals[triIndex] = glm::vec3(0, 0, 0);
				}
				LUA->Pop();

				LUA->GetField(-1, "tangent");
				if (LUA->IsType(-1, Type::Vector)) {
					triData.tangents[triIndex] = transformToBone(LUA->GetVector(), bones, bindBones, weights, true);
				} else {
					triData.tangents[triIndex] = glm::vec3(0, 0, 0);
				}
				LUA->Pop();

				LUA->GetField(-1, "binormal");
				if (LUA->IsType(-1, Type::Vector)) {
					triData.binormals[triIndex] = transformToBone(LUA->GetVector(), bones, bindBones, weights, true);
				} else {
					triData.binormals[triIndex] = glm::vec3(0, 0, 0);
				}
				LUA->Pop();

				// Get uvs
				LUA->GetField(-1, "u");
				LUA->GetField(-2, "v");
				float u = LUA->GetNumber(-2), v = LUA->GetNumber();
				LUA->Pop(2);
				triData.uvs[triIndex] = glm::vec2(u, v);

				// Pop MeshVertex
				LUA->Pop();

				// If this was the last vert in the tri, push back and validate normals, tangents, and binormals
				if (triIndex == 2U) {
					Triangle builtTri(
						Vector3{ tri[0].x, tri[0].y, tri[0].z },
						Vector3{ tri[1].x, tri[1].y, tri[1].z },
						Vector3{ tri[2].x, tri[2].y, tri[2].z }
					);

					// Check if triangle is invalid and remove vertices if so
					glm::vec3 geometricNormal{ builtTri.n[0], builtTri.n[1], builtTri.n[2] };
					if (!validVector(geometricNormal)) continue;


					glm::vec3& n0 = triData.normals[0], & n1 = triData.normals[1], & n2 = triData.normals[2];
					if (!validVector(n0) || glm::dot(n0, geometricNormal) < 0.01f) n0 = geometricNormal;
					if (!validVector(n1) || glm::dot(n1, geometricNormal) < 0.01f) n1 = geometricNormal;
					if (!validVector(n2) || glm::dot(n2, geometricNormal) < 0.01f) n2 = geometricNormal;

					glm::vec3& t0 = triData.tangents[0], & t1 = triData.tangents[1], & t2 = triData.tangents[2];
					if (
						!(
							validVector(t0) &&
							validVector(t1) &&
							validVector(t2)
						) ||
						fabsf(glm::dot(t0, n0)) > .9f ||
						fabsf(glm::dot(t1, n1)) > .9f ||
						fabsf(glm::dot(t2, n2)) > .9f
					) {
						glm::vec3 edge1 = tri[1] - tri[0];
						glm::vec3 edge2 = tri[2] - tri[0];

						glm::vec2 uv0 = triData.uvs[0], uv1 = triData.uvs[1], uv2 = triData.uvs[2];
						glm::vec2 dUV1 = uv1 - uv0;
						glm::vec2 dUV2 = uv2 - uv0;

						float f = 1.f / (dUV1.x * dUV2.y - dUV2.x * dUV1.y);

						glm::vec3 geometricTangent{
							f * (dUV2.y * edge1.x - dUV1.y * edge2.x),
							f * (dUV2.y * edge1.y - dUV1.y * edge2.y),
							f * (dUV2.y * edge1.z - dUV1.y * edge2.z)
						};
						if (!validVector(geometricTangent)) {
							// Set the tangent to one of the edges as a guess on the plane (this will only be reached if the uvs overlap)
							geometricTangent = glm::normalize(edge1);
							triData.ignoreNormalMap = true;
						}

						// Assign orthogonalised geometric tangent to vertices
						t0 = glm::normalize(geometricTangent - n0 * glm::dot(geometricTangent, n0));
						t1 = glm::normalize(geometricTangent - n1 * glm::dot(geometricTangent, n1));
						t2 = glm::normalize(geometricTangent - n2 * glm::dot(geometricTangent, n2));
					}

					glm::vec3& b0 = triData.binormals[0], & b1 = triData.binormals[1], & b2 = triData.binormals[2];
					if (!validVector(b0)) b0 = -glm::cross(n0, t0);
					if (!validVector(b1)) b1 = -glm::cross(n1, t1);
					if (!validVector(b2)) b2 = -glm::cross(n2, t2);

					mTriangles.push_back(builtTri);
					mTriangleData.push_back(triData);
				}
			}

			// Pop triangle and mesh tables
			LUA->Pop(2);

			// Get material
			std::string materialPath = "";
			LUA->GetField(-4, "GetMaterial");
			LUA->Push(-5);
			LUA->Call(1, 1);
			if (LUA->IsType(-1, Type::String)) materialPath = LUA->GetString();
			LUA->Pop();
			if (materialPath.empty()) {
				LUA->GetField(-4, "GetSubMaterial");
				LUA->Push(-5);
				LUA->PushNumber(meshIndex - 1U);
				LUA->Call(2, 1);
				if (LUA->IsType(-1, Type::String)) materialPath = LUA->GetString();
				LUA->Pop();

				if (materialPath.empty()) {
					LUA->GetField(-4, "GetMaterials");
					LUA->Push(-5);
					LUA->PushNumber(meshIndex);
					LUA->Call(2, 1);
					LUA->PushNumber(meshIndex);
					LUA->GetTable(-2);
					if (LUA->IsType(-1, Type::String)) materialPath = LUA->GetString();
					LUA->Pop(2); // Path and table
				}
			}
			if (materialPath.empty()) LUA->ThrowError("Entity has empty material");

			if (mMaterialIds.find(materialPath) == mMaterialIds.end()) {
				LUA->GetField(-3, "Material");
				LUA->PushString(materialPath.c_str());
				LUA->Call(1, 1);
				if (!LUA->IsType(-1, Type::Material)) LUA->ThrowError("Invalid material on entity");

				Material mat{};
				mat.maskedBlending = false;

				std::string baseTexture = getMaterialString(LUA, "$basetexture");
				std::string normalMap = getMaterialString(LUA, "$bumpmap");

				mat.baseTexture = CacheTexture(baseTexture, mTextureCache, mTextureCache[MISSING_TEXTURE]);
				mat.normalMap = CacheTexture(normalMap, mTextureCache);

				LUA->GetField(-1, "GetInt");
				LUA->Push(-2);
				LUA->PushString("$flags");
				LUA->Call(2, 1);
				if (LUA->IsType(-1, Type::Number)) mat.flags = static_cast<MaterialFlags>(LUA->GetNumber());
				LUA->Pop();

				LUA->Pop();

				mMaterialIds.emplace(materialPath, mMaterials.size());
				mMaterials.push_back(mat);
			}

			entData.materials.push_back(mMaterialIds[materialPath]);
		}

		LUA->Pop(3); // Pop meshes, util, and _G tables

		// Save the entity's pointer for hit verification later
		entData.rawEntity = LUA->GetUserType<CBaseEntity>(-1, Type::Entity);
		LUA->Pop(); // Pop entity

		mEntities.push_back(entData);
	}
	LUA->Pop(); // Pop entity table

	// Build BVH
	mAccel = BVH();
	bvh::SweepSahBuilder<BVH> builder(mAccel);
	auto [bboxes, centers] = bvh::compute_bounding_boxes_and_centers(mTriangles.data(), mTriangles.size());
	auto global_bbox = bvh::compute_bounding_boxes_union(bboxes.get(), mTriangles.size());
	builder.build(global_bbox, bboxes.get(), centers.get(), mTriangles.size());

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

	if (tMin < 0.f) LUA->ArgError(4, "tMin cannot be less than 0");
	if (tMax <= tMin) LUA->ArgError(5, "tMax must be greater than tMin");

	LUA->Pop(LUA->Top()); // Clear the stack of any items

	Ray ray(
		Vector3(origin.x, origin.y, origin.z),
		Vector3(direction.x, direction.y, direction.z),
		tMin,
		tMax
	);

	// Perform BVH traversal for mesh hit
	auto hit = mpTraverser->traverse(ray, *mpIntersector);
	if (hit) {
		auto intersection = hit->intersection;
		const Triangle& tri = mTriangles[hit->primitive_index];
		const TriangleData& triData = mTriangleData[hit->primitive_index];
		const Entity& ent = mEntities[triData.entIdx];
		const Material& mat = mMaterials[ent.materials[triData.submatIdx]];

		TraceResult res(
			glm::normalize(glm::vec3(direction.x, direction.y, direction.z)),
			tri, triData,
			glm::vec2(hit->intersection.u, hit->intersection.v),
			ent, mat
		);

		LUA->PushUserType_Value(res, TraceResult::id);
		return 1;
	}

	return 0;
}

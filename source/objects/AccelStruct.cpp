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

	LUA->PushSpecial(SPECIAL_GLOB);
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
			LUA->GetField(-1, "Material");
			LUA->PushString(tex.path);
			LUA->Call(1, 1);
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

			LUA->Pop();

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
	/*for (int i = 0; i < pMap->GetNumStaticProps(); i++) {
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

		// Iterate over meshes
		LUA->GetField(-1, "util"); // _G util
		LUA->GetField(-1, "GetModelMeshes"); // _G util GetModelMeshes
		LUA->PushString(prop.model); // _G util GetModelMeshes modelName
		LUA->Call(1, 2); // _G util meshes binds

		// Make sure both return values are present and valid
		if (!LUA->IsType(-2, Type::Table)) {
			LUA->Pop(2); // Pop the 2 nils (_G util)

			LUA->GetField(-1, "GetModelMeshes"); // _G util GetModelMeshes
			LUA->PushString("models/error.mdl"); // _G util GetModelMeshes modelName
			LUA->Call(1, 2); // _G util meshes binds

			if (!LUA->IsType(-2, Type::Table)) LUA->ThrowError("Entity model invalid and error model not available"); // This would only ever happen if the user's game is corrupt
		}
		if (!LUA->IsType(-1, Type::Table)) LUA->ThrowError("Entity bind pose not returned (this likely means you're running an older version of GMod)");

		// Cache bind pose
		auto bindBones = std::vector<glm::mat4>(1);
		LUA->PushNumber(0); // _G util meshes binds idx
		LUA->GetTable(-2);  // _G util meshes binds bind1
		LUA->GetField(-1, "matrix"); // _G util meshes binds bind1 bindMatrix

		glm::mat4 bind = glm::identity<glm::mat4>();
		if (LUA->IsType(-1, Type::Matrix)) {
			const VMatrix* pMat = LUA->GetUserType<VMatrix>(-1, Type::Matrix);
			bind = pMat->To4x4();
		}
		LUA->Pop(3); // _G util meshes

		size_t numSubmeshes = LUA->ObjLen();
		for (size_t meshIndex = 1; meshIndex <= numSubmeshes; meshIndex++) {
			// Get mesh
			LUA->PushNumber(meshIndex); // _G util meshes meshIdx
			LUA->GetTable(-2); // _G util meshes mesh

			// Iterate over tris
			LUA->GetField(-1, "triangles"); // _G util meshes mesh triangles
			if (!LUA->IsType(-1, Type::Table)) LUA->ThrowError("Vertices tables must contain MeshVertex tables");
			size_t numVerts = LUA->ObjLen();
			if (numVerts % 3U != 0U) LUA->ThrowError("Number of vertices is not a multiple of 3");

			glm::vec3 tri[3];
			TriangleData triData{};
			triData.entIdx = entities.size();
			triData.submatIdx = meshIndex - 1;
			triData.alphas[0] = triData.alphas[1] = triData.alphas[2] = 1.f;
			for (size_t vertIndex = 0; vertIndex < numVerts; vertIndex++) {
				// Get vertex
				LUA->PushNumber(vertIndex + 1U); // _G util meshes mesh triangles vertIdx
				LUA->GetTable(-2); // _G util meshes mesh triangles vertex

				// Get and transform position
				LUA->GetField(-1, "pos"); // _G util meshes mesh triangles vertex pos
				if (!LUA->IsType(-1, Type::Vector)) LUA->ThrowError("Invalid vertex in model mesh");
				Vector pos = LUA->GetVector();
				LUA->Pop(); // _G util meshes mesh triangles vertex

				size_t triIndex = vertIndex % 3U;
				tri[triIndex] = TransformToBone(pos, bone, bind);

				// Get and transform normal, tangent, and binormal
				LUA->GetField(-1, "normal"); // _G util meshes mesh triangles vertex normal
				if (LUA->IsType(-1, Type::Vector)) {
					Vector v = LUA->GetVector();
					triData.normals[triIndex] = TransformToBone(v, bone, bind, true);
				} else {
					triData.normals[triIndex] = glm::vec3(0, 0, 0);
				}
				LUA->Pop(); // _G util meshes mesh triangles vertex

				LUA->GetField(-1, "tangent"); // _G util meshes mesh triangles vertex tangent
				if (LUA->IsType(-1, Type::Vector)) {
					Vector v = LUA->GetVector();
					triData.tangents[triIndex] = TransformToBone(v, bone, bind, true);
				} else {
					triData.tangents[triIndex] = glm::vec3(0, 0, 0);
				}
				LUA->Pop(); // _G util meshes mesh triangles vertex

				LUA->GetField(-1, "binormal"); // _G util meshes mesh triangles vertex binormal
				if (LUA->IsType(-1, Type::Vector)) {
					Vector v = LUA->GetVector();
					triData.binormals[triIndex] = TransformToBone(v, bone, bind, true);
				} else {
					triData.binormals[triIndex] = glm::vec3(0, 0, 0);
				}
				LUA->Pop(); // _G util meshes mesh triangles vertex

				// Get uvs
				LUA->GetField(-1, "u"); // _G util meshes mesh triangles vertex u
				LUA->GetField(-2, "v"); // _G util meshes mesh triangles vertex u v
				float u = LUA->GetNumber(-2), v = LUA->GetNumber();
				LUA->Pop(2); // _G util meshes mesh triangles vertex
				triData.uvs[triIndex] = glm::vec2(u, v);

				// Pop MeshVertex
				LUA->Pop(); // _G util meshes mesh triangles

				// If this was the last vert in the tri, push back and validate normals, tangents, and binormals
				if (triIndex == 2U) {
					Triangle builtTri(
						Vector3{ tri[0].x, tri[0].y, tri[0].z },
						Vector3{ tri[1].x, tri[1].y, tri[1].z },
						Vector3{ tri[2].x, tri[2].y, tri[2].z },
						triData
					);

					// Check if triangle is invalid and remove vertices if so
					glm::vec3 geometricNormal{ builtTri.nNorm[0], builtTri.nNorm[1], builtTri.nNorm[2] };
					if (!ValidVector(geometricNormal)) continue;

					glm::vec3& n0 = builtTri.data.normals[0], & n1 = builtTri.data.normals[1], & n2 = builtTri.data.normals[2];
					if (!ValidVector(n0) || glm::dot(n0, geometricNormal) < 0.01f) n0 = geometricNormal;
					if (!ValidVector(n1) || glm::dot(n1, geometricNormal) < 0.01f) n1 = geometricNormal;
					if (!ValidVector(n2) || glm::dot(n2, geometricNormal) < 0.01f) n2 = geometricNormal;

					glm::vec3& t0 = builtTri.data.tangents[0], & t1 = builtTri.data.tangents[1], & t2 = builtTri.data.tangents[2];
					if (
						!(
							ValidVector(t0) &&
							ValidVector(t1) &&
							ValidVector(t2)
							) ||
						fabsf(glm::dot(t0, n0)) > .9f ||
						fabsf(glm::dot(t1, n1)) > .9f ||
						fabsf(glm::dot(t2, n2)) > .9f
						) {
						glm::vec3 edge1 = tri[1] - tri[0];
						glm::vec3 edge2 = tri[2] - tri[0];

						glm::vec2 uv0 = builtTri.data.uvs[0], uv1 = builtTri.data.uvs[1], uv2 = builtTri.data.uvs[2];
						glm::vec2 dUV1 = uv1 - uv0;
						glm::vec2 dUV2 = uv2 - uv0;

						float f = 1.f / (dUV1.x * dUV2.y - dUV2.x * dUV1.y);

						glm::vec3 geometricTangent{
							f * (dUV2.y * edge1.x - dUV1.y * edge2.x),
							f * (dUV2.y * edge1.y - dUV1.y * edge2.y),
							f * (dUV2.y * edge1.z - dUV1.y * edge2.z)
						};
						if (!ValidVector(geometricTangent)) {
							// Set the tangent to one of the edges as a guess on the plane (this will only be reached if the uvs overlap)
							geometricTangent = glm::normalize(edge1);
							builtTri.data.ignoreNormalMap = true;
						}

						// Assign orthogonalised geometric tangent to vertices
						t0 = glm::normalize(geometricTangent - n0 * glm::dot(geometricTangent, n0));
						t1 = glm::normalize(geometricTangent - n1 * glm::dot(geometricTangent, n1));
						t2 = glm::normalize(geometricTangent - n2 * glm::dot(geometricTangent, n2));
					}

					glm::vec3& b0 = builtTri.data.binormals[0], & b1 = builtTri.data.binormals[1], & b2 = builtTri.data.binormals[2];
					if (!ValidVector(b0)) b0 = -glm::cross(n0, t0);
					if (!ValidVector(b1)) b1 = -glm::cross(n1, t1);
					if (!ValidVector(b2)) b2 = -glm::cross(n2, t2);

					triangles.push_back(builtTri);
				}
			}

			// Pop triangle table
			LUA->Pop(); // _G util meshes mesh

			// Get material
			LUA->GetField(-1, "material"); // _G util meshes mesh material
			if (!LUA->IsType(-1, Type::String)) LUA->ThrowError("Submesh has no material");
			std::string materialPath = LUA->GetString();
			LUA->Pop(2); // _G util meshes

			if (materialIds.find(materialPath) == materialIds.end()) {
				LUA->GetField(-3, "Material"); // _G util meshes Material
				LUA->PushString(materialPath.c_str()); // _G util meshes Material materialPath
				LUA->Call(1, 1); // _G util meshes IMaterial
				if (!LUA->IsType(-1, Type::Material)) LUA->ThrowError("Invalid material on entity");

				IMaterial* sourceMaterial = LUA->GetUserType<IMaterial>(-1, Type::Material);
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

				LUA->Pop(); // _G util meshes

				materialIds.emplace(materialPath, materials.size());
				materials.push_back(mat);
			}

			entData.materials.push_back(materialIds[materialPath]);
		}

		LUA->Pop(2); // _G
		entities.push_back(entData);
	}
	*/

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

		// Get material
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

				// Recompute geometric normal and lod
				tri.n = cross(tri.e1, tri.e2);

				glm::vec2 uv10 = tri.uvs[1] - tri.uvs[0];
				glm::vec2 uv20 = tri.uvs[2] - tri.uvs[0];
				float triUVArea = abs(uv10.x * uv20.y - uv20.x * uv10.y);

				float len = length(tri.n);
				tri.lod = 0.5f * log2(triUVArea / len);
				tri.nNorm = Vector3(tri.n[0] / len, tri.n[1] / len, tri.n[2] / len);
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

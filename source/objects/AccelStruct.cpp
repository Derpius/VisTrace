#include <stdexcept>

#include "GMFS.h"

#include "AccelStruct.h"
#include "Utils.h"

#include "TraceResult.h"

#include "bvh/locally_ordered_clustering_builder.hpp"
#include "bvh/leaf_collapser.hpp"

#include "glm/gtx/euler_angles.hpp"

#define MISSING_TEXTURE "debug/debugempty"
#define WATER_BASE_TEXTURE "models/debug/debugwhite"

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
	const Vector& vec,
	const glm::mat4& bone, const glm::mat4& bind,
	const bool angleOnly = false
)
{
	glm::vec4 vertex = glm::vec4(vec.x, vec.y, vec.z, angleOnly ? 0.f : 1.f);
	return glm::vec3(bone * bind * vertex);
}

glm::vec3 TransformToBone(
	const Vector& vec,
	const std::vector<glm::mat4>& bones, const std::vector<glm::mat4>& binds,
	const std::vector<std::pair<size_t, float>>& weights,
	const bool angleOnly = false
)
{
	glm::vec4 final(0.f);
	glm::vec4 vertex = glm::vec4(vec.x, vec.y, vec.z, angleOnly ? 0.f : 1.f);
	for (size_t i = 0U; i < weights.size(); i++) {
		final += bones[weights[i].first] * binds[weights[i].first] * vertex * weights[i].second;
	}
	return glm::vec3(final);
}

const IVTFTexture* CacheTexture(
	const std::string& path,
	std::unordered_map<std::string, const IVTFTexture*>& cache,
	const IVTFTexture* fallback = nullptr
)
{
	if (cache.find(path) != cache.end()) return cache[path];

	if (!path.empty()) {
		const IVTFTexture* pTexture = new VTFTextureWrapper(path);
		if (pTexture->IsValid()) {
			cache.emplace(path, pTexture);
			return pTexture;
		}
		delete pTexture;
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

	entities = std::vector<Entity>();

	textureCache = std::unordered_map<std::string, const IVTFTexture*>();

	materials = std::vector<Material>();

	{
		const IVTFTexture* pTexture = new VTFTextureWrapper(MISSING_TEXTURE);
		if (!pTexture->IsValid()) {
			delete pTexture;
			delete pMap;
			pMap = nullptr;
			return;
		}
		textureCache.emplace(MISSING_TEXTURE, pTexture);

		pTexture = new VTFTextureWrapper(WATER_BASE_TEXTURE);
		if (pTexture->IsValid()) {
			textureCache.emplace(WATER_BASE_TEXTURE, pTexture);
		} else {
			delete pTexture;
		}
	}

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
	for (size_t triIdx = 0; triIdx < pMap->GetNumTris(); triIdx++) {
		size_t vi0 = triIdx * 3;
		size_t vi1 = vi0 + 1, vi2 = vi0 + 2;

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
		} catch (std::out_of_range e) {
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
			if (!LUA->IsType(-1, Type::Material)) {
				delete pMap;
				pMap = nullptr;
				for (auto& [key, element] : textureCache) {
					delete element;
				}
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

				mat.baseTexture = CacheTexture(mat.baseTexPath, textureCache, textureCache[MISSING_TEXTURE]);
				mat.normalMap = CacheTexture(mat.normalMapPath, textureCache);
				if (!mat.baseTexPath.empty()) mat.mrao = CacheTexture("vistrace/pbr/" + mat.baseTexPath + "_mrao", textureCache);

				mat.baseTexture2 = CacheTexture(mat.baseTexPath2, textureCache);
				mat.normalMap2 = CacheTexture(mat.normalMapPath2, textureCache);
				if (!mat.baseTexPath2.empty()) mat.mrao2 = CacheTexture("vistrace/pbr/" + mat.baseTexPath2 + "_mrao", textureCache);

				mat.blendTexture = CacheTexture(mat.blendTexPath, textureCache);
				mat.detail = CacheTexture(mat.detailPath, textureCache);

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

				mat.baseTexture = CacheTexture(
					mat.baseTexPath, textureCache,
					(textureCache.find(WATER_BASE_TEXTURE) == textureCache.end()) ? textureCache[MISSING_TEXTURE] : textureCache[WATER_BASE_TEXTURE]
				);
				mat.normalMap = CacheTexture(mat.normalMapPath, textureCache);
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
		triData.submatIdx = submatIds[strPath];

		// Construct bvh tri
		triangles.push_back(Triangle(
			Vector3{ vertices[vi0].x, vertices[vi0].y, vertices[vi0].z },
			Vector3{ vertices[vi1].x, vertices[vi1].y, vertices[vi1].z },
			Vector3{ vertices[vi2].x, vertices[vi2].y, vertices[vi2].z },
			triData,

			// Backface cull on the world to prevent z fighting on 2 sided water surfaces
			// (given you shouldnt be refracting through any other brushes this should be fine)
			true
		));
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

				mat.baseTexture = CacheTexture(mat.baseTexPath, textureCache, textureCache[MISSING_TEXTURE]);
				mat.normalMap = CacheTexture(mat.normalMapPath, textureCache);
				mat.detail = CacheTexture(mat.detailPath, textureCache);
				if (!mat.baseTexPath.empty()) mat.mrao = CacheTexture("vistrace/pbr/" + mat.baseTexPath + "_mrao", textureCache);

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

	LUA->Pop(); // Pop _G
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

	mEntities = std::vector<Entity>();

	mTextureCache = std::unordered_map<std::string, const IVTFTexture*>();

	mMaterialIds = std::unordered_map<std::string, size_t>();
	mMaterials = std::vector<Material>();
}

AccelStruct::~AccelStruct()
{
	if (mAccelBuilt) {
		delete mpIntersector;
		delete mpTraverser;
	}
	for (const auto& [key, val] : mTextureCache) {
		if (mpWorld != nullptr && mpWorld->textureCache.find(key) != mpWorld->textureCache.end()) continue;
		delete val;
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
	for (const auto& [key, val] : mTextureCache) {
		if (mpWorld != nullptr && mpWorld->textureCache.find(key) != mpWorld->textureCache.end()) continue;
		delete val;
	}

	mTriangles.erase(mTriangles.begin(), mTriangles.end());

	mEntities.erase(mEntities.begin(), mEntities.end());

	mTextureCache.erase(mTextureCache.begin(), mTextureCache.end());

	mMaterialIds.erase(mMaterialIds.begin(), mMaterialIds.end());
	mMaterials.erase(mMaterials.begin(), mMaterials.end());

	if (mpWorld != nullptr) {
		mTriangles = mpWorld->triangles;
		mEntities = mpWorld->entities;
		mTextureCache = mpWorld->textureCache;
		mMaterials = mpWorld->materials;
	} else {
		{
			const IVTFTexture* pTexture = new VTFTextureWrapper(MISSING_TEXTURE);
			if (!pTexture->IsValid()) {
				delete pTexture;
				LUA->ThrowError("Failed to read missing texture");
			}
			mTextureCache.emplace(MISSING_TEXTURE, pTexture);
		}
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
				const VMatrix* pMat = LUA->GetUserType<VMatrix>(-1, Type::Matrix);
				transform = pMat->To4x4();
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
				const VMatrix* pMat = LUA->GetUserType<VMatrix>(-1, Type::Matrix);
				transform = pMat->To4x4();
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
			triData.submatIdx = meshIndex - 1;
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
				tri[triIndex] = TransformToBone(LUA->GetVector(), bones, bindBones, weights);

				LUA->Pop();

				// Get and transform normal, tangent, and binormal
				LUA->GetField(-1, "normal");
				if (LUA->IsType(-1, Type::Vector)) {
					triData.normals[triIndex] = TransformToBone(LUA->GetVector(), bones, bindBones, weights, true);
				} else {
					triData.normals[triIndex] = glm::vec3(0, 0, 0);
				}
				LUA->Pop();

				LUA->GetField(-1, "tangent");
				if (LUA->IsType(-1, Type::Vector)) {
					triData.tangents[triIndex] = TransformToBone(LUA->GetVector(), bones, bindBones, weights, true);
				} else {
					triData.tangents[triIndex] = glm::vec3(0, 0, 0);
				}
				LUA->Pop();

				LUA->GetField(-1, "binormal");
				if (LUA->IsType(-1, Type::Vector)) {
					triData.binormals[triIndex] = TransformToBone(LUA->GetVector(), bones, bindBones, weights, true);
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

					mTriangles.push_back(builtTri);
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

				// Grab the source material
				IMaterial* sourceMaterial = LUA->GetUserType<IMaterial>(-1, Type::Material);

				Material mat{};
				mat.path = materialPath;
				mat.maskedBlending = false;

				mat.baseTexPath = GetMaterialString(sourceMaterial, "$basetexture");
				mat.normalMapPath = GetMaterialString(sourceMaterial, "$bumpmap");
				mat.detailPath = GetMaterialString(sourceMaterial, "$detail");

				mat.baseTexture = CacheTexture(mat.baseTexPath, mTextureCache, mTextureCache[MISSING_TEXTURE]);
				mat.normalMap = CacheTexture(mat.normalMapPath, mTextureCache);
				mat.detail = CacheTexture(mat.detailPath, mTextureCache);
				if (!mat.baseTexPath.empty()) mat.mrao = CacheTexture("vistrace/pbr/" + mat.baseTexPath + "_mrao", mTextureCache);

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

		LUA->Pop(3); // Pop meshes, util, and _G tables

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
		tMin,
		tMax
	);
	ray.pAccel = this;

	// Perform BVH traversal for mesh hit
	auto hit = mpTraverser->traverse(ray, *mpIntersector);
	if (hit) {
		auto intersection = hit->intersection;
		const Triangle& tri = mTriangles[hit->primitive_index];
		const TriangleData& triData = tri.data;
		const Entity& ent = mEntities[triData.entIdx];
		const Material& mat = mMaterials[ent.materials[triData.submatIdx]];

		TraceResult* pRes = new TraceResult(
			glm::normalize(glm::vec3(direction.x, direction.y, direction.z)), hit->distance(),
			coneWidth, coneAngle,
			tri, triData,
			glm::vec2(hit->intersection.u, hit->intersection.v),
			ent, mat
		);

		LUA->PushUserType_Value(pRes, TraceResult::id);
		return 1;
	}

	return 0;
}

Material AccelStruct::GetMaterial(const TriangleData& triData) const
{
	return mMaterials[mEntities[triData.entIdx].materials[triData.submatIdx]];
}

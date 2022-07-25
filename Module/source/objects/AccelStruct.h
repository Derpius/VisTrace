#pragma once

#include <vector>
#include <unordered_map>
#include <string>

#include "GarrysMod/Lua/Interface.h"

#include "bvh/bvh.hpp"
#include "bvh/vector.hpp"
#include "bvh/triangle.hpp"
#include "bvh/ray.hpp"
#include "bvh/sweep_sah_builder.hpp"
#include "bvh/single_ray_traverser.hpp"
#include "bvh/primitive_intersectors.hpp"

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"

#include "VTFParser.h"

using Vector3 = bvh::Vector3<float>;
using Triangle = bvh::Triangle<float>;
using Ray = bvh::Ray<float>;
using BVH = bvh::Bvh<float>;

using Intersector = bvh::ClosestPrimitiveIntersector<BVH, Triangle>;
using Traverser = bvh::SingleRayTraverser<BVH>;

typedef void CBaseEntity;

struct TriangleData
{
	glm::vec3 normals[3];
	glm::vec3 tangents[3];
	glm::vec3 binormals[3];

	glm::vec2 uvs[3];

	size_t entIdx;
	uint32_t submatIdx;

	bool ignoreNormalMap;
};

struct Material
{
	size_t baseTexture = 0;
	size_t normalMap = 0;
	uint32_t flags = 0;
};

struct Entity
{
	CBaseEntity* rawEntity;
	uint32_t id;

	std::vector<size_t> materials;
	glm::vec4 colour;
};

class AccelStruct
{
	bool mAccelBuilt;
	BVH mAccel;
	Intersector* mpIntersector;
	Traverser* mpTraverser;

	std::vector<Triangle> mTriangles;
	std::vector<TriangleData> mTriangleData;

	std::vector<Entity> mEntities;

	std::unordered_map<std::string, size_t> mTextureIds;
	std::vector<VTFTexture*> mTextureCache;

	std::unordered_map<std::string, size_t> mMaterialIds;
	std::vector<Material> mMaterials;

public:
	AccelStruct();
	~AccelStruct();

	void PopulateAccel(GarrysMod::Lua::ILuaBase* LUA);
	int Traverse(GarrysMod::Lua::ILuaBase* LUA);
};

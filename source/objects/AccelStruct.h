#pragma once

#include <vector>
#include <unordered_map>
#include <string>

#include "GarrysMod/Lua/Interface.h"

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"

#include "BSPParser.h"

#include "Utils.h"
#include "VTFTexture.h"
#include "SourceTypes.h"
#include "vistrace/IRenderTarget.h"

#include "Material.h"
#include "Primitives.h"
#include "Model.h"

#include "bvh/sweep_sah_builder.hpp"
#include "bvh/single_ray_traverser.hpp"
#include "bvh/primitive_intersectors.hpp"

using BVH = bvh::Bvh<float>;
using Ray = bvh::Ray<float>;

using Intersector = bvh::ClosestPrimitiveIntersector<BVH, Triangle>;
using Traverser = bvh::SingleRayTraverser<BVH>;

struct Entity
{
	CBaseEntity* rawEntity;
	uint32_t id;

	std::vector<size_t> materials;
	glm::vec4 colour;
};

class World
{
private:
	BSPMap* pMap;

public:
	std::vector<Triangle> triangles;

	std::vector<Entity> entities;

	std::unordered_map<std::string, size_t> materialIds;
	std::vector<Material> materials;

	World(GarrysMod::Lua::ILuaBase* LUA, const std::string& mapName);
	~World();

	bool IsValid() const;
};

class AccelStruct
{
private:
	const World* mpWorld;

	bool mAccelBuilt;
	BVH mAccel;
	Intersector* mpIntersector;
	Traverser* mpTraverser;

	std::vector<Triangle> mTriangles;

	std::vector<Entity> mEntities;

	std::unordered_map<std::string, size_t> mMaterialIds;
	std::vector<Material> mMaterials;

public:
	AccelStruct();
	~AccelStruct();

	void PopulateAccel(GarrysMod::Lua::ILuaBase* LUA, const World* pWorld = nullptr);
	int Traverse(GarrysMod::Lua::ILuaBase* LUA);

	const Material& GetMaterial(const size_t i) const;
};

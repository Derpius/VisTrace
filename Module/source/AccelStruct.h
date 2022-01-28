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

#include "filesystem.h"

#include "VTFParser.h"

using Vector3 = bvh::Vector3<float>;
using Triangle = bvh::Triangle<float>;
using Ray = bvh::Ray<float>;
using BVH = bvh::Bvh<float>;

using Intersector = bvh::ClosestPrimitiveIntersector<BVH, Triangle>;
using Traverser = bvh::SingleRayTraverser<BVH>;

class AccelStruct
{
	IFileSystem* mpFileSystem;

	bool mAccelBuilt;
	BVH mAccel;
	Intersector* mpIntersector;
	Traverser* mpTraverser;

	std::vector<Triangle> mTriangles;
	std::vector<glm::vec3> mNormals;
	std::vector<glm::vec3> mTangents;
	std::vector<glm::vec3> mBinormals;
	std::vector<glm::vec2> mUvs;

	std::vector<std::pair<unsigned int, unsigned int>> mIds; // first: entity id, second: submesh id
	std::unordered_map<unsigned int, size_t> mOffsets; // Offset for each entity id into per-submesh tables

	std::unordered_map<std::string, VTFTexture*> mTexCache;
	std::vector<std::string> mMaterials;

public:
	AccelStruct(IFileSystem* pFileSystem);
	~AccelStruct();

	void PopulateAccel(GarrysMod::Lua::ILuaBase* LUA);
	void Traverse(GarrysMod::Lua::ILuaBase* LUA);
};

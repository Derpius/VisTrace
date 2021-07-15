#pragma once

#include <vector>
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

using Vector3 = bvh::Vector3<float>;
using Triangle = bvh::Triangle<float>;
using Ray = bvh::Ray<float>;
using BVH = bvh::Bvh<float>;

using Intersector = bvh::ClosestPrimitiveIntersector<BVH, Triangle>;
using Traverser = bvh::SingleRayTraverser<BVH>;

class AccelStruct
{
	bool accelBuilt;
	BVH accel;
	Intersector* pIntersector;
	Traverser* pTraverser;

	std::vector<Triangle> triangles;
	std::vector<Vector3> normals;
	std::vector<Vector3> tangents;
	std::vector<Vector3> binormals;
	std::vector<std::pair<float, float>> uvs;
	std::vector<std::pair<unsigned int, unsigned int>> ids; // first: entity id, second: submesh id

public:
	AccelStruct();
	~AccelStruct();

	void PopulateAccel(GarrysMod::Lua::ILuaBase* LUA);
	void Traverse(GarrysMod::Lua::ILuaBase* LUA);
};

#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <optional>

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
#include "BSPParser.h"

#include "Utils.h"

using Vector3 = bvh::Vector3<float>;
using Ray = bvh::Ray<float>;
using BVH = bvh::Bvh<float>;

/// <summary>
/// Extension of BVH lib's Triangle primitive to implement backface culling
/// </summary>
/// <typeparam name="Scalar"></typeparam>
template <typename Scalar, bool LeftHandedNormal = true, bool NonZeroTolerance = false>
struct TriangleBackfaceCull
{
	struct Intersection
	{
		Scalar t, u, v;
		Scalar distance() const { return t; }
	};

	using ScalarType = Scalar;
	using IntersectionType = Intersection;

	bvh::Vector3<Scalar> p0, e1, e2, n;
	bool oneSided = false;

	TriangleBackfaceCull() = default;
	TriangleBackfaceCull(
		const bvh::Vector3<Scalar>& p0,
		const bvh::Vector3<Scalar>& p1,
		const bvh::Vector3<Scalar>& p2,
		const bool oneSided = false
	) : p0(p0), e1(p0 - p1), e2(p2 - p0), oneSided(oneSided)
	{
		n = LeftHandedNormal ? cross(e1, e2) : cross(e2, e1);
	}

	bvh::Vector3<Scalar> p1() const { return p0 - e1; }
	bvh::Vector3<Scalar> p2() const { return p0 + e2; }

	bvh::BoundingBox<Scalar> bounding_box() const
	{
		bvh::BoundingBox<Scalar> bbox(p0);
		bbox.extend(p1());
		bbox.extend(p2());
		return bbox;
	}

	bvh::Vector3<Scalar> center() const
	{
		return (p0 + p1() + p2()) * (Scalar(1.0) / Scalar(3.0));
	}

	std::pair<bvh::Vector3<Scalar>, bvh::Vector3<Scalar>> edge(size_t i) const
	{
		assert(i < 3);
		bvh::Vector3<Scalar> p[] = { p0, p1(), p2() };
		return std::make_pair(p[i], p[(i + 1) % 3]);
	}

	Scalar area() const
	{
		return length(n) * Scalar(0.5);
	}

	std::pair<bvh::BoundingBox<Scalar>, bvh::BoundingBox<Scalar>> split(size_t axis, Scalar position) const
	{
		bvh::Vector3<Scalar> p[] = { p0, p1(), p2() };
		auto left = bvh::BoundingBox<Scalar>::empty();
		auto right = bvh::BoundingBox<Scalar>::empty();
		auto split_edge = [=](const bvh::Vector3<Scalar>& a, const bvh::Vector3<Scalar>& b) {
			auto t = (position - a[axis]) / (b[axis] - a[axis]);
			return a + t * (b - a);
		};
		auto q0 = p[0][axis] <= position;
		auto q1 = p[1][axis] <= position;
		auto q2 = p[2][axis] <= position;
		if (q0) left.extend(p[0]);
		else    right.extend(p[0]);
		if (q1) left.extend(p[1]);
		else    right.extend(p[1]);
		if (q2) left.extend(p[2]);
		else    right.extend(p[2]);
		if (q0 ^ q1) {
			auto m = split_edge(p[0], p[1]);
			left.extend(m);
			right.extend(m);
		}
		if (q1 ^ q2) {
			auto m = split_edge(p[1], p[2]);
			left.extend(m);
			right.extend(m);
		}
		if (q2 ^ q0) {
			auto m = split_edge(p[2], p[0]);
			left.extend(m);
			right.extend(m);
		}
		return std::make_pair(left, right);
	}

	std::optional<Intersection> intersect(const bvh::Ray<Scalar>& ray) const
	{
		auto negate_when_right_handed = [](Scalar x) { return LeftHandedNormal ? x : -x; };

		auto nDotDir = dot(n, ray.direction);
		if (oneSided && nDotDir > 0) return std::nullopt;

		auto c = p0 - ray.origin;
		auto r = cross(ray.direction, c);
		auto inv_det = negate_when_right_handed(1.0) / nDotDir;

		auto u = dot(r, e2) * inv_det;
		auto v = dot(r, e1) * inv_det;
		auto w = Scalar(1.0) - u - v;

		// These comparisons are designed to return false
		// when one of t, u, or v is a NaN
		static constexpr auto tolerance = NonZeroTolerance ? -std::numeric_limits<Scalar>::epsilon() : Scalar(0);
		if (u >= tolerance && v >= tolerance && w >= tolerance) {
			auto t = negate_when_right_handed(dot(n, c)) * inv_det;
			if (t >= ray.tmin && t <= ray.tmax) {
				if constexpr (NonZeroTolerance) {
					u = robust_max(u, Scalar(0));
					v = robust_max(v, Scalar(0));
				}
				return std::make_optional(Intersection{ t, u, v });
			}
		}

		return std::nullopt;
	}
};

using Triangle = TriangleBackfaceCull<float>;
using Intersector = bvh::ClosestPrimitiveIntersector<BVH, Triangle>;
using Traverser = bvh::SingleRayTraverser<BVH>;

typedef void CBaseEntity;

struct TriangleData
{
	glm::vec3 normals[3];
	glm::vec3 tangents[3];
	glm::vec3 binormals[3];

	glm::vec2 uvs[3];
	float alphas[3];

	size_t entIdx;
	uint32_t submatIdx;

	bool ignoreNormalMap;
};

// Minimal implementation of Valve's matrix type for efficiently reading from the stack
struct VMatrix
{
	typedef float vec_t;
	vec_t m[4][4];

	inline glm::mat2x4 To2x4() const
	{
		return glm::mat2x4(
			m[0][0], m[0][1], m[0][2], m[0][3],
			m[1][0], m[1][1], m[1][2], m[1][3]
		);
	}

	inline glm::mat4 To4x4() const
	{
		return glm::mat4(
			m[0][0], m[1][0], m[2][0], m[3][0],
			m[0][1], m[1][1], m[2][1], m[3][1],
			m[0][2], m[1][2], m[2][2], m[3][2],
			m[0][3], m[1][3], m[2][3], m[3][3]
		);
	}

	/// <summary>
	/// Gets a VMatrix from the material at the top of the stack
	/// </summary>
	/// <param name="LUA">Lua instance</param>
	/// <param name="key">Material key</param>
	/// <returns>VMatrix pointer on success or nullptr on failure</returns>
	static VMatrix* FromMaterial(GarrysMod::Lua::ILuaBase* LUA, const std::string& key);
};

struct Material
{
	glm::vec4 colour = glm::vec4(1, 1, 1, 1);
	VTFTexture* baseTexture   = nullptr;
	glm::mat2x4 baseTexMat    = glm::identity<glm::mat2x4>();
	VTFTexture* normalMap     = nullptr;
	glm::mat2x4 normalMapMat  = glm::identity<glm::mat2x4>();
	VTFTexture* mrao          = nullptr;
	//glm::mat2x4 mraoMat     = glm::identity<glm::mat2x4>(); MRAO texture lookups are driven by the base texture

	VTFTexture* baseTexture2  = nullptr;
	glm::mat2x4 baseTexMat2   = glm::identity<glm::mat2x4>();
	VTFTexture* normalMap2    = nullptr;
	glm::mat2x4 normalMapMat2 = glm::identity<glm::mat2x4>();
	VTFTexture* mrao2         = nullptr;
	//glm::mat2x4 mraoMat2    = glm::identity<glm::mat2x4>(); MRAO texture lookups are driven by the base texture

	VTFTexture* blendTexture  = nullptr;
	glm::mat2x4 blendTexMat   = glm::identity<glm::mat2x4>();
	bool maskedBlending;

	float texScale = 1.f;

	MaterialFlags flags      = MaterialFlags::NONE;
	BSPEnums::SURF surfFlags = BSPEnums::SURF::NONE;
	bool water = false;
};

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
	std::vector<TriangleData> triangleData;

	std::vector<Entity> entities;

	std::unordered_map<std::string, VTFTexture*> textureCache;

	std::unordered_map<std::string, size_t> materialIds;
	std::vector<Material> materials;

	World(GarrysMod::Lua::ILuaBase* LUA, const std::string& mapName);
	~World();

	bool IsValid() const;
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

	std::unordered_map<std::string, VTFTexture*> mTextureCache;

	std::unordered_map<std::string, size_t> mMaterialIds;
	std::vector<Material> mMaterials;

public:
	AccelStruct();
	~AccelStruct();

	void PopulateAccel(GarrysMod::Lua::ILuaBase* LUA, const World* pWorld = nullptr);
	int Traverse(GarrysMod::Lua::ILuaBase* LUA);
};

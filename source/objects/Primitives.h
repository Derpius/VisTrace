#pragma once

#include "Material.h"

class AccelStruct;
// Custom ray to pass additional data to the intersector
#define BVH_RAY_HPP
#include "bvh/vector.hpp"
namespace bvh
{
	template <typename Scalar>
	struct Ray
	{
		Vector3<Scalar> origin;
		Vector3<Scalar> direction;
		Scalar tmin;
		Scalar tmax;

		const AccelStruct* pAccel = nullptr;

		Ray() = default;
		Ray(const Vector3<Scalar>& origin,
			const Vector3<Scalar>& direction,
			const AccelStruct* pAccel,
			Scalar tmin = Scalar(0),
			Scalar tmax = std::numeric_limits<Scalar>::max()
		) : origin(origin), direction(direction), pAccel(pAccel), tmin(tmin), tmax(tmax)
		{
		}
	};
}

#include "bvh/bvh.hpp"
#include "glm/glm.hpp"

#include <optional>

/// <summary>
/// Extension of BVH lib's Triangle primitive
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

	bvh::Vector3<Scalar> p0, e1, e2, n, nNorm;
	bool oneSided = false;
	bool ignoreNormalMap = false;

	size_t material;
	uint16_t entIdx = 0;

	glm::vec3 normals[3];
	glm::vec3 tangents[3];

	glm::vec2 uvs[3];
	float alphas[3];

	uint8_t numBones[3];
	float weights[3][3];
	int8_t boneIds[3][3];

	float lod;

	TriangleBackfaceCull() = default;
	TriangleBackfaceCull(
		const bvh::Vector3<Scalar> p0,
		const bvh::Vector3<Scalar> p1,
		const bvh::Vector3<Scalar> p2,
		const int16_t material,
		const bool oneSided = false
	) : p0(p0), e1(p0 - p1), e2(p2 - p0), material(material), oneSided(oneSided)
	{
		n = LeftHandedNormal ? cross(e1, e2) : cross(e2, e1);

		glm::vec2 uv10 = uvs[1] - uvs[0];
		glm::vec2 uv20 = uvs[2] - uvs[0];
		float triUVArea = abs(uv10.x * uv20.y - uv20.x * uv10.y);

		Scalar len = length(n);
		lod = 0.5f * log2(triUVArea / len);
		nNorm = bvh::Vector3<Scalar>(n[0] / len, n[1] / len, n[2] / len);
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
		const Material& mat = ray.pAccel->GetMaterial(material);
		auto negate_when_right_handed = [](Scalar x) { return LeftHandedNormal ? x : -x; };

		auto nDotDir = dot(n, ray.direction);
		if (oneSided && (mat.flags & MaterialFlags::nocull) == MaterialFlags::NONE && nDotDir > 0) return std::nullopt;

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

				// Material has alpha test flag, check the base texture and discard this hit if less than 255 alpha
				if ((mat.flags & MaterialFlags::alphatest) != MaterialFlags::NONE) {
					// Calculate texture UVs - Should these be cached in the primitive to avoid recalculation later, or left out to save memory?
					glm::vec2 texUV = (1.f - u - v) * uvs[0] + u * uvs[1] + v * uvs[2];
					texUV = TransformTexcoord(texUV, mat.baseTexMat, mat.texScale);

					// Was mipmapping here but with trilinear it looked like shit
					float alpha = mat.baseTexture->Sample(texUV.x, texUV.y, 0.f).a;

					// See: https://developer.valvesoftware.com/wiki/$alphatest#Comparison
					if (alpha < mat.alphatestreference) {
						return std::nullopt;
					}
				}

				return std::make_optional(Intersection{ t, u, v });
			}
		}

		return std::nullopt;
	}
};
using Triangle = TriangleBackfaceCull<float>;
using Vector3 = bvh::Vector3<float>;

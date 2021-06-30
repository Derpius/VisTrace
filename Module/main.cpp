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

using namespace GarrysMod::Lua;
using Vector3 = bvh::Vector3<float>;
using Triangle = bvh::Triangle<float>;
using Ray = bvh::Ray<float>;
using BVH = bvh::Bvh<float>;

using Intersector = bvh::ClosestPrimitiveIntersector<BVH, Triangle>;
using Traverser = bvh::SingleRayTraverser<BVH>;

static bool accelBuilt = false;
static std::shared_ptr<BVH> pAccelStruct;
static std::shared_ptr<Intersector> pIntersector;
static std::shared_ptr<Traverser> pTraverser;

static std::vector<Triangle> triangles;
static std::vector<Vector3> normals;
static std::vector<Vector3> tangents;
static std::vector<Vector3> binormals;
static std::vector<std::pair<float ,float>> uvs;
static std::vector<std::pair<unsigned int, unsigned int>> ids; // first: entity id, second: submesh id

void printLua(ILuaBase* inst, const char text[])
{
	inst->PushSpecial(SPECIAL_GLOB);
	inst->GetField(-1, "print");
	inst->PushString(text);
	inst->Call(1, 0);
	inst->Pop();
}

void normalise(Vector3& v)
{
	float length = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	v[0] /= length;
	v[1] /= length;
	v[2] /= length;
}

// Skins a vertex to its bones
Vector3 transformToBone(
	const Vector& vec,
	const std::vector<glm::mat4>& bones, const std::vector<glm::mat4>& binds,
	const std::vector<std::pair<size_t, float>>& weights,
	const bool angleOnly = false
)
{
	glm::vec4 final(0.f);
	for (size_t i = 0U; i < weights.size(); i++) {
		final += bones[weights[i].first] * binds[weights[i].first] * glm::vec4(vec.x, vec.y, vec.z, angleOnly ? 0.f : 1.f) * weights[i].second;
	}
	return Vector3(final.x, final.y, final.z);
}

/*
	table[Entity] entities = {}
*/
LUA_FUNCTION(RebuildAccel)
{
	if (LUA->Top() == 0) LUA->CreateTable();
	else if (LUA->IsType(1, Type::Nil)) {
		LUA->Pop(LUA->Top());
		LUA->CreateTable();
	} else {
		LUA->CheckType(1, Type::Table);
		LUA->Pop(LUA->Top() - 1); // Pop all but the table
	}

	// Delete accel
	accelBuilt = false;
	pAccelStruct = nullptr;
	pIntersector = nullptr;
	pTraverser = nullptr;

	// Redefine vectors
	triangles = std::vector<Triangle>();
	normals = std::vector<Vector3>();
	tangents = std::vector<Vector3>();
	binormals = std::vector<Vector3>();
	uvs = std::vector<std::pair<float, float>>();
	ids = std::vector<std::pair<unsigned int, unsigned int>>();

	// Iterate over entities
	size_t numEntities = LUA->ObjLen();
	for (size_t entIndex = 1; entIndex <= numEntities; entIndex++) {
		// Get entity
		LUA->PushNumber(entIndex);
		LUA->GetTable(1);
		LUA->CheckType(-1, Type::Entity);

		// Make sure entity is valid
		LUA->GetField(-1, "IsValid");
		LUA->Push(-2);
		LUA->Call(1, 1);
		if (!LUA->GetBool()) LUA->ThrowError("Attempted to build accel from an invalid entity");
		LUA->Pop(); // Pop the bool

		// Get entity id
		std::pair<unsigned int, unsigned int> id;
		{
			LUA->GetField(-1, "EntIndex");
			LUA->Push(-2);
			LUA->Call(1, 1);
			double entId = LUA->CheckNumber(); // Get as a double so after we check it's positive a static cast to unsigned int wont overflow rather than using int
			LUA->Pop();

			if (entId < 0.0) LUA->ThrowError("Entity ID is less than 0");
			id.first = entId;
		}

		// Cache bone transforms
		// Make sure the bone transforms are updated and the bones themselves are valid
		LUA->GetField(-1, "SetupBones");
		LUA->Push(-2);
		LUA->Call(1, 0);

		// Get number of bones and make sure the value is valid
		LUA->GetField(-1, "GetBoneCount");
		LUA->Push(-2);
		LUA->Call(1, 1);
		int numBones = LUA->CheckNumber();
		LUA->Pop();
		if (numBones < 1) LUA->ThrowError("Entity has invalid bones");

		// For each bone, cache the transform
		auto bones = std::vector<glm::mat4>(numBones);
		for (int boneIndex = 0; boneIndex < numBones; boneIndex++) {
			LUA->GetField(-1, "GetBoneMatrix");
			LUA->Push(-2);
			LUA->PushNumber(boneIndex);
			LUA->Call(2, 1);

			glm::mat4 transform = glm::mat4();
			for (unsigned char row = 0; row < 4; row++) {
				for (unsigned char col = 0; col < 4; col++) {
					LUA->GetField(-1, "GetField");
					LUA->Push(-2);
					LUA->PushNumber(row + 1);
					LUA->PushNumber(col + 1);
					LUA->Call(3, 1);

					transform[col][row] = LUA->CheckNumber();
					LUA->Pop();
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
		if (!LUA->IsType(-2, Type::Table)) LUA->ThrowError("Entity model invalid");
		if (!LUA->IsType(-1, Type::Table)) LUA->ThrowError("Entity model valid, but bind pose not returned (this likely means you're running an older version of GMod)");

		// Cache bind pose
		auto bindBones = std::vector<glm::mat4>(numBones);
		for (int boneIndex = 0; boneIndex < numBones; boneIndex++) {
			LUA->PushNumber(boneIndex);
			LUA->GetTable(-2);
			LUA->GetField(-1, "matrix");

			glm::mat4 transform = glm::mat4();
			for (unsigned char row = 0; row < 4; row++) {
				for (unsigned char col = 0; col < 4; col++) {
					LUA->GetField(-1, "GetField");
					LUA->Push(-2);
					LUA->PushNumber(row + 1);
					LUA->PushNumber(col + 1);
					LUA->Call(3, 1);

					transform[col][row] = LUA->CheckNumber();
					LUA->Pop();
				}
			}

			bindBones[boneIndex] = transform;
			LUA->Pop(2);
		}
		LUA->Pop();

		size_t numSubmeshes = LUA->ObjLen();
		for (size_t meshIndex = 1; meshIndex <= numSubmeshes; meshIndex++) {
			// Get mesh
			LUA->PushNumber(meshIndex);
			LUA->GetTable(-2);

			// Set submesh id
			id.second = meshIndex - 1U;

			// Iterate over tris
			LUA->GetField(-1, "triangles");
			LUA->CheckType(-1, Type::Table);
			size_t numVerts = LUA->ObjLen();
			if (numVerts % 3U != 0U) LUA->ThrowError("Number of triangles is not a multiple of 3");

			Vector3 tri[3];
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
						weights.emplace_back(LUA->CheckNumber(-2), LUA->CheckNumber());
						LUA->Pop(3);
					}
				}
				LUA->Pop();

				// Get and transform position
				LUA->GetField(-1, "pos");
				Vector pos = LUA->GetVector();
				LUA->Pop();

				size_t triIndex = vertIndex % 3U;
				tri[triIndex] = transformToBone(pos, bones, bindBones, weights);

				// Get and transform normal, tangent, and binormal
				LUA->GetField(-1, "normal");
				Vector normal;
				if (!LUA->IsType(-1, Type::Nil)) {
					normal = LUA->GetVector();
				} else {
					normal.x = normal.y = normal.z = 0.f;
				}
				LUA->Pop();
				normals.push_back(transformToBone(normal, bones, bindBones, weights, true));

				LUA->GetField(-1, "tangent");
				Vector tangent;
				if (!LUA->IsType(-1, Type::Nil)) {
					tangent = LUA->GetVector();
				} else {
					tangent.x = tangent.y = tangent.z = 0.f;
				}
				LUA->Pop();
				tangents.push_back(transformToBone(normal, bones, bindBones, weights, true));

				LUA->GetField(-1, "binormal");
				Vector binormal;
				if (!LUA->IsType(-1, Type::Nil)) {
					binormal = LUA->GetVector();
				} else {
					binormal.x = binormal.y = binormal.z = 0.f;
				}
				LUA->Pop();
				binormals.push_back(transformToBone(normal, bones, bindBones, weights, true));

				// Get uvs
				LUA->GetField(-1, "u");
				LUA->GetField(-2, "v");
				float u = LUA->GetNumber(-2), v = LUA->GetNumber();
				LUA->Pop(2);
				uvs.emplace_back(u, v);

				// Push back copy of ids
				ids.push_back(id);

				// Pop MeshVertex
				LUA->Pop();

				// If this was the last vert in the tri, emplace back
				if (triIndex == 2U) {
					Triangle builtTri(tri[0], tri[1], tri[2]);
					triangles.push_back(builtTri);

					// in the unlikely event a mesh has no vertex normals, the normal at this point would be 0, 0, 0
					// if so, replace it with the tri's geometric normal
					size_t numNorms = normals.size();
					Vector3 n0 = normals[numNorms - 2U], n1 = normals[numNorms - 1U], n2 = normals[numNorms];
					if (
						n0[0] == 0.f && n0[1] == 0.f && n0[2] == 0.f &&
						n1[0] == 0.f && n1[1] == 0.f && n1[2] == 0.f &&
						n2[0] == 0.f && n2[1] == 0.f && n2[2] == 0.f
					) normals[numNorms - 2U] = normals[numNorms - 1U] = normals[numNorms] = builtTri.n;
				}
			}

			// Pop triangle and mesh tables
			LUA->Pop(2);
		}

		LUA->Pop(3); // Pop meshes, util, and _G tables

		// Mark the entity as present in accel (for ent id verification later)
		LUA->PushBool(true);
		LUA->SetField(-2, "vistrace_mark");
		LUA->Pop(); // Pop entity
	}
	LUA->Pop(); // Pop entity table

	// Build BVH
	pAccelStruct = std::make_shared<BVH>(BVH());
	bvh::SweepSahBuilder<BVH> builder(*pAccelStruct);
	auto [bboxes, centers] = bvh::compute_bounding_boxes_and_centers(triangles.data(), triangles.size());
	auto global_bbox = bvh::compute_bounding_boxes_union(bboxes.get(), triangles.size());
	builder.build(global_bbox, bboxes.get(), centers.get(), triangles.size());

	pIntersector = std::make_shared<Intersector>(Intersector(*pAccelStruct, triangles.data()));
	pTraverser = std::make_shared<Traverser>(Traverser(*pAccelStruct));

	accelBuilt = true;
	return 0;
}

/*
	Vector origin
	Vector direction
	float tMin = 0
	float tMax = FLT_MAX
	bool hitWorld = true

	returns modified TraceResult struct (https://wiki.facepunch.com/gmod/Structures/TraceResult)
*/
LUA_FUNCTION(TraverseScene)
{
	if (!accelBuilt) LUA->ThrowError("Unable to perform traversal, acceleration structure not built (use vistrace.RebuildAccel to build one)");
	int numArgs = LUA->Top();

	// Parse arguments
	LUA->CheckType(1, Type::Vector);
	LUA->CheckType(2, Type::Vector);

	Vector origin = LUA->GetVector(1);
	Vector direction = LUA->GetVector(2);

	float tMin = 0.f;
	if (numArgs > 2 && !LUA->IsType(3, Type::Nil)) tMin = static_cast<float>(LUA->CheckNumber(3));
	
	float tMax = FLT_MAX;
	if (numArgs > 3 && !LUA->IsType(4, Type::Nil)) tMax = static_cast<float>(LUA->CheckNumber(4));

	if (tMin < 0.f) LUA->ArgError(3, "tMin cannot be less than 0");
	if (tMax <= tMin) LUA->ArgError(4, "tMax must be greater than tMin");

	bool hitWorld = true;
	if (numArgs > 4 && !LUA->IsType(5, Type::Nil)) {
		LUA->CheckType(5, Type::Bool);
		hitWorld = LUA->GetBool(5);
	}

	LUA->Pop(LUA->Top()); // Clear the stack of any items

	Ray ray(
		Vector3(origin.x, origin.y, origin.z),
		Vector3(direction.x, direction.y, direction.z),
		tMin,
		tMax
	);

#pragma region Create Default TraceResult
	LUA->CreateTable();

	// Set TraceResult.Entity to a NULL entity
	LUA->PushSpecial(SPECIAL_GLOB);
	LUA->GetField(-1, "Entity");
	LUA->PushNumber(-1); // Getting entity at index -1 *should* always return a NULL entity
	LUA->Call(1, 1);
	LUA->SetField(1, "Entity");
	LUA->Pop();

	LUA->PushNumber(1.0);
	LUA->SetField(1, "Fraction");

	LUA->PushNumber(0.0);
	LUA->SetField(1, "FractionLeftSolid");

	LUA->PushBool(false);
	LUA->SetField(1, "Hit");

	LUA->PushNumber(0);
	LUA->SetField(1, "HitBox");

	LUA->PushNumber(0);
	LUA->SetField(1, "HitGroup");

	LUA->PushBool(false);
	LUA->SetField(1, "HitNoDraw");

	LUA->PushBool(false);
	LUA->SetField(1, "HitNonWorld");

	{
		Vector v;
		v.x = v.y = v.z = 0.f;
		LUA->PushVector(v);
		LUA->SetField(1, "HitNormal");
	}

	{
		Vector3 endPos = ray.origin + ray.direction * ray.tmax;
		Vector v;
		v.x = endPos[0];
		v.y = endPos[1];
		v.z = endPos[2];
		LUA->PushVector(v);
		LUA->SetField(1, "HitPos");
	}

	LUA->PushBool(false);
	LUA->SetField(1, "HitSky");

	LUA->PushString("** empty **");
	LUA->SetField(1, "HitTexture");

	LUA->PushBool(false);
	LUA->SetField(1, "HitWorld");

	LUA->PushNumber(0);
	LUA->SetField(1, "MatType");

	{
		Vector v;
		v.x = ray.direction[0];
		v.y = ray.direction[1];
		v.z = ray.direction[2];
		LUA->PushVector(v);
		LUA->SetField(1, "Normal");
	}

	LUA->PushNumber(0);
	LUA->SetField(1, "PhysicsBone");

	{
		Vector3 start = ray.origin + ray.direction * ray.tmin;
		Vector v;
		v.x = start[0];
		v.y = start[1];
		v.z = start[2];
		LUA->PushVector(v);
		LUA->SetField(-2, "StartPos");
	}

	LUA->PushNumber(0);
	LUA->SetField(1, "SurfaceProps");

	LUA->PushBool(false);
	LUA->SetField(1, "StartSolid");

	LUA->PushBool(false);
	LUA->SetField(1, "AllSolid");

	LUA->PushNumber(0);
	LUA->SetField(1, "SurfaceFlags");

	LUA->PushNumber(0);
	LUA->SetField(1, "DispFlags");

	LUA->PushNumber(1);
	LUA->SetField(1, "Contents");

	// Custom members for uvs
	LUA->CreateTable();
		LUA->PushNumber(0);
		LUA->SetField(-2, "u");
		LUA->PushNumber(0);
		LUA->SetField(-2, "v");
	LUA->SetField(1, "HitTexCoord");

	LUA->CreateTable();
		LUA->PushNumber(0);
		LUA->SetField(-2, "u");
		LUA->PushNumber(0);
		LUA->SetField(-2, "v");
	LUA->SetField(1, "HitBarycentric");

	// Custom members for tangent binormal, and geometric normal
	{
		Vector v;
		v.x = v.y = v.z = 0.f;
		LUA->PushVector(v);
		LUA->SetField(1, "HitTangent");
		LUA->PushVector(v);
		LUA->SetField(1, "HitBinormal");
		LUA->PushVector(v);
		LUA->SetField(1, "HitNormalGeometric");
	}

	// Custom member for the submat id
	LUA->PushNumber(0);
	LUA->SetField(1, "SubmatIndex");

	// EntIndex field allows the user to lookup entity data that they've cached themselves, even if the hit entity is now invalid
	LUA->PushNumber(-1);
	LUA->SetField(1, "EntIndex");
#pragma endregion

	// Perform source trace for world hit
	if (hitWorld) {
		// Get TraceLine function
		LUA->PushSpecial(SPECIAL_GLOB);
		LUA->GetField(-1, "util");
		LUA->GetField(-1, "TraceLine");

		// Create Trace struct that will only hit brushes (https://wiki.facepunch.com/gmod/Structures/Trace)
		LUA->CreateTable();

		{
			Vector3 start = ray.origin + ray.direction * ray.tmin;
			Vector v;
			v.x = start[0];
			v.y = start[1];
			v.z = start[2];
			LUA->PushVector(v);
			LUA->SetField(-2, "start");
		}

		{
			Vector3 endPos = ray.origin + ray.direction * ray.tmax;
			Vector v;
			v.x = endPos[0];
			v.y = endPos[1];
			v.z = endPos[2];
			LUA->PushVector(v);
			LUA->SetField(-2, "endpos");
		}

		LUA->CreateTable();
		LUA->SetField(-2, "filter");

		LUA->PushNumber(16395 /* MASK_SOLID_BRUSHONLY */);
		LUA->SetField(-2, "mask");

		LUA->PushNumber(0);
		LUA->SetField(-2, "collisiongroup");

		LUA->PushBool(false);
		LUA->SetField(-2, "ignoreworld");

		LUA->Call(1, 1);

		// Get new BVH tMax from world hit (to prevent hitting meshes behind the world)
		LUA->GetField(-1, "Hit");
		if (LUA->GetBool()) {
			LUA->GetField(-2, "Fraction");
			ray.tmax *= LUA->GetNumber();
			LUA->Pop();
		}
		LUA->Pop();

		// Even though we have no world uvs, set the parameters to 0 anyway to avoid erroring code that would normally use them
		LUA->CreateTable();
			LUA->PushNumber(0);
			LUA->SetField(-2, "u");
			LUA->PushNumber(0);
			LUA->SetField(-2, "v");
		LUA->SetField(-2, "HitTexCoord");

		LUA->CreateTable();
			LUA->PushNumber(0);
			LUA->SetField(-2, "u");
			LUA->PushNumber(0);
			LUA->SetField(-2, "v");
		LUA->SetField(-2, "HitBarycentric");

		// And same for the other custom members
		{
			Vector v;
			v.x = v.y = v.z = 0.f;
			LUA->PushVector(v);
			LUA->SetField(-2, "HitTangent");
			LUA->PushVector(v);
			LUA->SetField(-2, "HitBinormal");
		}

		// Copy HitNormal to HitNormalGeometric
		{
			LUA->GetField(-1, "HitNormal");
			LUA->SetField(-2, "HitNormalGeometric");
		}

		LUA->PushNumber(0);
		LUA->SetField(-2, "SubmatIndex");

		LUA->PushNumber(0);
		LUA->SetField(-2, "EntIndex");
	}

	// Perform BVH traversal for mesh hit
	auto hit = pTraverser->traverse(ray, *pIntersector);
	if (hit) {
		auto intersection = hit->intersection;
		size_t vertIndex = hit->primitive_index * 3;

		// If hitWorld is true, pop all the data from the stack for the world hit (world hit logic modifies the ray's tMax, so no need to compare distances if this managed to hit)
		if (hitWorld) LUA->Pop(3);

		// Populate TraceResult struct with what we can
		{
			LUA->PushSpecial(SPECIAL_GLOB);
			LUA->GetField(-1, "Entity");
			LUA->PushNumber(ids[vertIndex].first);
			LUA->Call(1, 1);

			LUA->GetField(-1, "vistrace_mark");
			bool markedEnt = LUA->GetBool();
			LUA->Pop();

			if (!markedEnt) {
				LUA->Pop(); // Pop the invalid entity (not necessarily an invalid entity, but not the same as what was at that index when accel was built)
				LUA->GetField(-1, "Entity");
				LUA->PushNumber(-1);
				LUA->Call(1, 1);
			}

			LUA->SetField(1, "Entity");

			LUA->Pop(); // Pop _G
		}

		LUA->PushNumber(ids[vertIndex].first);
		LUA->SetField(1, "EntIndex");

		LUA->PushNumber(intersection.t / tMax);
		LUA->SetField(1, "Fraction");

		LUA->PushBool(true);
		LUA->SetField(1, "Hit");

		LUA->PushBool(true);
		LUA->SetField(1, "HitNonWorld");

		{
			Vector3 hitPos = ray.origin + ray.direction * intersection.t;
			Vector v;
			v.x = hitPos[0];
			v.y = hitPos[1];
			v.z = hitPos[2];
			LUA->PushVector(v);
			LUA->SetField(1, "HitPos");
		}

		float u = intersection.u, v = intersection.v, w = (1.f - u - v);
		{
			Vector3 normal = w * normals[vertIndex] + u * normals[vertIndex + 1U] + v * normals[vertIndex + 2U];
			normalise(normal);

			Vector v;
			v.x = normal[0];
			v.y = normal[1];
			v.z = normal[2];
			LUA->PushVector(v);
			LUA->SetField(1, "HitNormal");
		}

		// Push custom hitdata values for tangent and binormal
		{
			Vector3 tangent = w * tangents[vertIndex] + u * tangents[vertIndex + 1U] + v * tangents[vertIndex + 2U];
			Vector3 binormal = w * binormals[vertIndex] + u * binormals[vertIndex + 1U] + v * binormals[vertIndex + 2U];
			if (tangent[0] != 0.f || tangent[1] != 0.f || tangent[2] != 0.f) normalise(tangent);
			if (binormal[0] != 0.f || binormal[1] != 0.f || binormal[2] != 0.f) normalise(binormal);

			Vector v;

			v.x = tangent[0];
			v.y = tangent[1];
			v.z = tangent[2];
			LUA->PushVector(v);
			LUA->SetField(1, "HitTangent");

			v.x = binormal[0];
			v.y = binormal[1];
			v.z = binormal[2];
			LUA->PushVector(v);
			LUA->SetField(1, "HitBinormal");
		}

		// Push custom hitdata values for U and V (texture and barycentric)
		float texU = w * uvs[vertIndex].first + u * uvs[vertIndex + 1U].first + v * uvs[vertIndex + 2U].first;
		float texV = w * uvs[vertIndex].second + u * uvs[vertIndex + 1U].second + v * uvs[vertIndex + 2U].second;
		texU -= floor(texU);
		texV -= floor(texV);

		LUA->CreateTable();
			LUA->PushNumber(texU);
			LUA->SetField(-2, "u");
			LUA->PushNumber(texV);
			LUA->SetField(-2, "v");
		LUA->SetField(1, "HitTexCoord");

		LUA->CreateTable();
			LUA->PushNumber(u);
			LUA->SetField(-2, "u");
			LUA->PushNumber(v);
			LUA->SetField(-2, "v");
		LUA->SetField(1, "HitBarycentric");

		// Push custom hidata values for submat id
		LUA->PushNumber(ids[vertIndex].second);
		LUA->SetField(1, "SubmatIndex");

		// Push geometric normal of hit tri
		{
			Vector3 n = triangles[hit->primitive_index].n;
			normalise(n);

			Vector v;
			v.x = n[0];
			v.y = n[1];
			v.z = n[2];
			LUA->PushVector(v);
			LUA->SetField(1, "HitNormalGeometric");
		}
	}

	return 1; // Return the table at the top of the stack (this will either be a default TraceResult, a TraceResult populated by BVH intersection, or a TraceResult populated by world intersection)
}

GMOD_MODULE_OPEN()
{
	LUA->PushSpecial(SPECIAL_GLOB);
		LUA->CreateTable();
			LUA->PushCFunction(RebuildAccel);
			LUA->SetField(-2, "RebuildAccel");
			LUA->PushCFunction(TraverseScene);
			LUA->SetField(-2, "TraverseScene");
			LUA->PushString("v0.3.0");
			LUA->SetField(-2, "ModuleVersion");
		LUA->SetField(-2, "vistrace");
	LUA->Pop();

	printLua(LUA, "VisTrace Loaded!");
	return 0;
}

GMOD_MODULE_CLOSE()
{
	return 0;
}
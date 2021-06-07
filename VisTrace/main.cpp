#include <vector>
#include <string>

#include "GarrysMod/Lua/Interface.h"

#include <bvh/bvh.hpp>
#include <bvh/vector.hpp>
#include <bvh/triangle.hpp>
#include <bvh/ray.hpp>
#include <bvh/sweep_sah_builder.hpp>
#include <bvh/single_ray_traverser.hpp>
#include <bvh/primitive_intersectors.hpp>

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

void dumpStack(ILuaBase* inst)
{
	std::string toPrint = "";

	int max = inst->Top();
	for (int i = 1; i <= max; i++) {
		toPrint += "[" + std::to_string(i) + "] ";
		switch (inst->GetType(i)) {
		case Type::Angle:
			toPrint += "Angle: (" + std::to_string((int)inst->GetAngle(i).x) + ", " + std::to_string((int)inst->GetAngle(i).y) + ", " + std::to_string((int)inst->GetAngle(i).z) + ")";
			break;
		case Type::Vector:
			toPrint += "Vector: (" + std::to_string(inst->GetVector(i).x) + ", " + std::to_string(inst->GetVector(i).y) + ", " + std::to_string(inst->GetVector(i).z) + ")";
			break;
		case Type::Bool:
			toPrint += "Bool: " + std::to_string(inst->GetBool(i));
			break;
		case Type::Function:
			toPrint += "Function";
			break;
		case Type::Nil:
			toPrint += "nil";
			break;
		case Type::Number:
			toPrint += "Number: " + std::to_string(inst->GetNumber(i));
			break;
		case Type::String:
			toPrint += "String: " + (std::string)inst->GetString(i);
			break;
		case Type::Table:
			toPrint += "Table";
			break;
		case Type::Matrix:
			toPrint += "VMatrix";
			break;
		default:
			toPrint += "Unknown";
			break;
		}
		toPrint += "\n";
	}

	printLua(inst, toPrint.c_str());
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
	for (size_t i = 1; i <= numEntities; i++) {
		// Get entity
		LUA->PushNumber(i);
		LUA->GetTable(1);
		LUA->CheckType(-1, Type::Entity);

		// Get entity id
		std::pair<unsigned int, unsigned int> id;
		{
			LUA->GetField(-1, "EntIndex");
			LUA->Push(-2);
			LUA->Call(1, 1);
			double entId = LUA->GetNumber(); // Get as a double so after we check it's positive a static cast to unsigned int wont overflow rather than using int
			LUA->Pop();

			if (entId < 0.0) LUA->ThrowError("Entity ID is less than 0");
			id.first = entId;
		}

		// Get transform VMatrix
		LUA->GetField(-1, "GetWorldTransformMatrix");
		LUA->Push(-2);
		LUA->Call(1, 1);
		float transform[3][4];
		for (unsigned char row = 0; row < 3; row++) {
			for (unsigned char col = 0; col < 4; col++) {
				LUA->GetField(-1, "GetField");
				LUA->Push(-2);
				LUA->PushNumber(row + 1);
				LUA->PushNumber(col + 1);
				LUA->Call(3, 1);

				transform[row][col] = LUA->GetNumber();
				LUA->Pop();
			}
		}
		LUA->Pop();

		// Iterate over meshes
		LUA->PushSpecial(SPECIAL_GLOB);
		LUA->GetField(-1, "util");
		LUA->GetField(-1, "GetModelMeshes");
			LUA->GetField(-4, "GetModel");
			LUA->Push(-5);
			LUA->Call(1, 1);
		LUA->Call(1, 1);

		size_t numSubmeshes = LUA->ObjLen();
		for (size_t j = 1; j <= numSubmeshes; j++) {
			// Get mesh
			LUA->PushNumber(j);
			LUA->GetTable(-2);

			// Set submesh id
			id.second = j - 1Ui64;

			// Iterate over tris
			LUA->GetField(-1, "triangles");
			LUA->CheckType(-1, Type::Table);
			size_t numVerts = LUA->ObjLen();
			if (numVerts % 3Ui64 != 0Ui64) LUA->ThrowError("Number of triangles is not a multiple of 3");

			Vector3 tri[3];
			for (size_t j = 0; j < numVerts; j++) {
				// Get vertex
				LUA->PushNumber(j + 1Ui64);
				LUA->GetTable(-2);

				// Get and transform position
				LUA->GetField(-1, "pos");
				Vector pos = LUA->GetVector();
				LUA->Pop();

				size_t triIndex = j % 3Ui64;
				tri[triIndex] = Vector3(
					pos.x * transform[0][0] + pos.y * transform[0][1] + pos.z * transform[0][2] + transform[0][3],
					pos.x * transform[1][0] + pos.y * transform[1][1] + pos.z * transform[1][2] + transform[1][3],
					pos.x * transform[2][0] + pos.y * transform[2][1] + pos.z * transform[2][2] + transform[2][3]
				);

				// Get and transform normal, tangent, and binormal
				LUA->GetField(-1, "normal");
				Vector normal;
				if (!LUA->IsType(-1, Type::Nil)) {
					normal = LUA->GetVector();
				} else {
					normal.x = normal.y = normal.z = 0.f;
				}
				LUA->Pop();
				normals.push_back(Vector3(
					normal.x * transform[0][0] + normal.y * transform[0][1] + normal.z * transform[0][2],
					normal.x * transform[1][0] + normal.y * transform[1][1] + normal.z * transform[1][2],
					normal.x * transform[2][0] + normal.y * transform[2][1] + normal.z * transform[2][2]
				));

				LUA->GetField(-1, "tangent");
				Vector tangent;
				if (!LUA->IsType(-1, Type::Nil)) {
					tangent = LUA->GetVector();
				} else {
					tangent.x = tangent.y = tangent.z = 0.f;
				}
				LUA->Pop();
				tangents.push_back(Vector3(
					tangent.x * transform[0][0] + tangent.y * transform[0][1] + tangent.z * transform[0][2],
					tangent.x * transform[1][0] + tangent.y * transform[1][1] + tangent.z * transform[1][2],
					tangent.x * transform[2][0] + tangent.y * transform[2][1] + tangent.z * transform[2][2]
				));

				LUA->GetField(-1, "binormal");
				Vector binormal;
				if (!LUA->IsType(-1, Type::Nil)) {
					binormal = LUA->GetVector();
				} else {
					binormal.x = binormal.y = binormal.z = 0.f;
				}
				LUA->Pop();
				binormals.push_back(Vector3(
					binormal.x * transform[0][0] + binormal.y * transform[0][1] + binormal.z * transform[0][2],
					binormal.x * transform[1][0] + binormal.y * transform[1][1] + binormal.z * transform[1][2],
					binormal.x * transform[2][0] + binormal.y * transform[2][1] + binormal.z * transform[2][2]
				));

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
				if (triIndex == 2Ui64) {
					Triangle tri(tri[0], tri[1], tri[2]);
					triangles.push_back(tri);

					// in the unlikely event a mesh has no vertex normals, the normal at this point would be 0, 0, 0
					// if so, replace it with the tri's geometric normal (inverted as source uses anti-clockwise winding and from the looks of it the bvh uses CW, likely to match the "standard" winding)
					size_t numNorms = normals.size();
					Vector3 n0 = normals[numNorms - 2Ui64], n1 = normals[numNorms - 1Ui64], n2 = normals[numNorms];
					if (
						n0[0] == 0.f || n0[1] == 0.f || n0[2] == 0.f ||
						n1[0] == 0.f || n1[1] == 0.f || n1[2] == 0.f ||
						n2[0] == 0.f || n2[1] == 0.f || n2[2] == 0.f
					) normals[numNorms - 2Ui64] = normals[numNorms - 1Ui64] = normals[numNorms] = -tri.n;
				}
			}

			// Pop triangle and mesh tables
			LUA->Pop(2);
		}

		LUA->Pop(3);
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
	LUA->PushNumber(0);
	LUA->SetField(1, "HitU");

	LUA->PushNumber(0);
	LUA->SetField(1, "HitV");

	// Custom members for tangent and binormal
	{
		Vector v;
		v.x = v.y = v.z = 0.f;
		LUA->PushVector(v);
		LUA->SetField(1, "HitTangent");
		LUA->PushVector(v);
		LUA->SetField(1, "HitBinormal");
	}
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
		LUA->PushNumber(0);
		LUA->SetField(-2, "HitU");

		LUA->PushNumber(0);
		LUA->SetField(-2, "HitV");

		// And same for the other custom members
		{
			Vector v;
			v.x = v.y = v.z = 0.f;
			LUA->PushVector(v);
			LUA->SetField(-2, "HitTangent");
			LUA->PushVector(v);
			LUA->SetField(-2, "HitBinormal");
		}
	}

	// Perform BVH traversal for mesh hit
	auto hit = pTraverser->traverse(ray, *pIntersector);
	if (hit) {
		auto intersection = hit->intersection;
		size_t vertIndex = hit->primitive_index * 3;

		// If hitWorld is true, pop all the data from the stack for the world hit (world hit logic modifies the ray's tMax, so no need to compare distances if this managed to hit)
		if (hitWorld) LUA->Pop(3);

		// Populate TraceResult struct with what we can
		LUA->PushSpecial(SPECIAL_GLOB);
		LUA->GetField(-1, "Entity");
		LUA->PushNumber(ids[vertIndex].first);
		LUA->Call(1, 1);
		LUA->SetField(1, "Entity");
		LUA->Pop();

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
			Vector3 normal = w * normals[vertIndex] + u * normals[vertIndex + 1Ui64] + v * normals[vertIndex + 2Ui64];
			Vector v;
			v.x = normal[0];
			v.y = normal[1];
			v.z = normal[2];
			LUA->PushVector(v);
			LUA->SetField(1, "HitNormal");
		}

		// Push custom hitdata values for tangent and binormal
		{
			Vector3 tangent = w * tangents[vertIndex] + u * tangents[vertIndex + 1Ui64] + v * tangents[vertIndex + 2Ui64];
			Vector3 binormal = w * binormals[vertIndex] + u * binormals[vertIndex + 1Ui64] + v * binormals[vertIndex + 2Ui64];
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

		// Push custom hitdata values for U and V
		float texU = w * uvs[vertIndex].first + u * uvs[vertIndex + 1Ui64].first + v * uvs[vertIndex + 2Ui64].first;
		float texV = w * uvs[vertIndex].second + u * uvs[vertIndex + 1Ui64].second + v * uvs[vertIndex + 2Ui64].second;

		LUA->PushNumber(texU);
		LUA->SetField(1, "HitU");

		LUA->PushNumber(texV);
		LUA->SetField(1, "HitV");
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
		LUA->SetField(-2, "vistrace");

		// Check if Starfall is present, and if so sideload vistrace into the running instance
		LUA->GetField(-1, "SF");
		bool sf = LUA->IsType(-1, Type::Table);
		LUA->Pop();

		if (sf) {
			LUA->GetField(-1, "RunString");
			LUA->PushString(
				"local inf = math.huge\n"
				"local function validateVector(v)\n"
				"	if (\n"
				"		v[1] ~= v[1] or v[1] == inf or v[1] == -inf or\n"
				"		v[2] ~= v[2] or v[2] == inf or v[2] == -inf or\n"
				"		v[3] ~= v[3] or v[3] == inf or v[3] == -inf\n"
				"	) then SF.Throw(\"Invalid vector, inf or NaN present in function traverseScene\", 3) end\n"
				"end\n"

				"local checkLuaType, checkPermission = SF.CheckLuaType, SF.Permissions.check\n"
				"local debug_getmetatable = debug.getmetatable\n"

				"SF.Permissions.registerPrivilege(\"vistrace\", \"VisTrace\", \"Allows the user to build acceleration structures and traverse scenes\", { client = {default = 1} })\n"
				"SF.Permissions.loadPermissionOptions()\n"

				"SF.Modules.vistrace = {injected = {init = function(instance)\n"
				"	local env, uwrapEnt, uwrapVec, wrapObj = instance.env, instance.Types.Entity.Unwrap, instance.Types.Vector.Unwrap, instance.WrapObject\n"
				"	local entMetaTable, vecMetaTbl = instance.Types.Entity, instance.Types.Vector\n"
				"	env.vistrace = {\n"
				"		rebuildAccel = function(entities)\n"
				"			checkPermission(instance, nil, \"vistrace\")\n"

				"			if entities then\n"
				"				checkLuaType(entities, TYPE_TABLE)\n"
				"				local unwrapped = {}\n"
				"				for k, v in pairs(entities) do\n"
				"					if debug_getmetatable(v) ~= entMetaTable then SF.ThrowTypeError(\"Entity\", SF.GetType(v), 2, \"Entity table entry not an entity.\") end\n"
				"					unwrapped[k] = uwrapEnt(v)\n"
				"				end\n"
				"				entities = unwrapped\n"
				"			end\n"

				"			vistrace.RebuildAccel(entities)\n"
				"		end,\n"
				"		traverseScene = function(origin, direction, tMin, tMax, hitWorld)\n"
				"			checkPermission(instance, nil, \"vistrace\")\n"

				"			if debug_getmetatable(origin) ~= vecMetaTbl then SF.ThrowTypeError(\"Entity\", SF.GetType(origin), 2) end\n"
				"			validateVector(origin)\n"

				"			if debug_getmetatable(direction) ~= vecMetaTbl then SF.ThrowTypeError(\"Entity\", SF.GetType(direction), 2) end\n"
				"			validateVector(direction)\n"

				"			if tMin then checkLuaType(tMin, TYPE_NUMBER) end\n"
				"			if tMax then checkLuaType(tMax, TYPE_NUMBER) end\n"

				"			if hitWorld then checkLuaType(hitWorld, TYPE_BOOL) end\n"

				"			local hitData = vistrace.TraverseScene(uwrapVec(origin), uwrapVec(direction), tMin, tMax, hitWorld)\n"
				"			for k, v in pairs(hitData) do // Note that vistrace returns tables, not actual TraceResult structs, so we can just enumerate and wrap rather than using SF.StructWrapper\n"
				"				hitData[k] = wrapObj(v)\n"
				"			end\n"
				"			return hitData\n"
				"		end\n"
				"	}\n"
				"end}}"
			);
			LUA->Call(1, 0);
		}
	LUA->Pop();

	return 0;
}

GMOD_MODULE_CLOSE()
{
	return 0;
}
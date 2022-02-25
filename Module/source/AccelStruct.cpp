#include "AccelStruct.h"
#include "Utils.h"

#define MISSING_TEXTURE "debug/debugempty"

using namespace GarrysMod::Lua;

void normalise(Vector3& v)
{
	float length = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	v[0] /= length;
	v[1] /= length;
	v[2] /= length;
}

AccelStruct::AccelStruct(IFileSystem* pFileSystem)
{
	mpFileSystem = pFileSystem;
	mpIntersector = nullptr;
	mpTraverser = nullptr;
	mAccelBuilt = false;
}

AccelStruct::~AccelStruct()
{
	if (mAccelBuilt) {
		delete mpIntersector;
		delete mpTraverser;

		for (auto& element : mTexCache) {
			delete element.second;
		}
	}
}

void AccelStruct::PopulateAccel(ILuaBase* LUA)
{
	// Delete accel
	if (mAccelBuilt) {
		mAccelBuilt = false;
		delete mpIntersector;
		delete mpTraverser;

		for (auto& element : mTexCache) {
			delete element.second;
		}
	}

	// Redefine containers
	mTriangles = std::vector<Triangle>();
	mNormals = std::vector<glm::vec3>();
	mTangents = std::vector<glm::vec3>();
	mBinormals = std::vector<glm::vec3>();
	mUvs = std::vector<glm::vec2>();

	mIds = std::vector<std::pair<unsigned int, unsigned int>>();
	mOffsets = std::unordered_map<unsigned int, size_t>();

	mTexCache = std::unordered_map<std::string, VTFTexture*>();
	mMaterials = std::vector<std::string>();
	mNormalMapBlacklist = std::unordered_map<size_t, bool>();

	{
		VTFTexture* pTexture;
		if (!readTexture(MISSING_TEXTURE, mpFileSystem, &pTexture))
			LUA->ThrowError("Failed to read missing texture VTF");
		mTexCache[MISSING_TEXTURE] = pTexture;
	}

	// Iterate over entities
	size_t numEntities = LUA->ObjLen();
	size_t offset = 0;
	for (size_t entIndex = 1; entIndex <= numEntities; entIndex++) {
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

		mOffsets[id.first] = offset;

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
				for (unsigned char row = 0; row < 4; row++) {
					for (unsigned char col = 0; col < 4; col++) {
						LUA->GetField(-1, "GetField");
						LUA->Push(-2);
						LUA->PushNumber(row + 1);
						LUA->PushNumber(col + 1);
						LUA->Call(3, 1);

						transform[col][row] = LUA->GetNumber();
						LUA->Pop();
					}
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
				for (unsigned char row = 0; row < 4; row++) {
					for (unsigned char col = 0; col < 4; col++) {
						LUA->GetField(-1, "GetField");
						LUA->Push(-2);
						LUA->PushNumber(row + 1);
						LUA->PushNumber(col + 1);
						LUA->Call(3, 1);

						transform[col][row] = LUA->GetNumber();
						LUA->Pop();
					}
				}
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

			// Set submesh id
			id.second = meshIndex - 1U;

			// Iterate over tris
			LUA->GetField(-1, "triangles");
			if (!LUA->IsType(-1, Type::Table)) LUA->ThrowError("Vertices tables must contain MeshVertex tables");
			size_t numVerts = LUA->ObjLen();
			if (numVerts % 3U != 0U) LUA->ThrowError("Number of vertices is not a multiple of 3");

			glm::vec3 tri[3];
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
				tri[triIndex] = transformToBone(LUA->GetVector(), bones, bindBones, weights);

				LUA->Pop();

				// Get and transform normal, tangent, and binormal
				LUA->GetField(-1, "normal");
				if (LUA->IsType(-1, Type::Vector)) {
					mNormals.push_back(transformToBone(LUA->GetVector(), bones, bindBones, weights, true));
				} else {
					mNormals.emplace_back(0, 0, 0);
				}
				LUA->Pop();

				LUA->GetField(-1, "tangent");
				if (LUA->IsType(-1, Type::Vector)) {
					mTangents.push_back(transformToBone(LUA->GetVector(), bones, bindBones, weights, true));
				} else {
					mTangents.emplace_back(0, 0, 0);
				}
				LUA->Pop();

				LUA->GetField(-1, "binormal");
				if (LUA->IsType(-1, Type::Vector)) {
					mBinormals.push_back(transformToBone(LUA->GetVector(), bones, bindBones, weights, true));
				} else {
					mBinormals.emplace_back(0, 0, 0);
				}
				LUA->Pop();

				// Get uvs
				LUA->GetField(-1, "u");
				LUA->GetField(-2, "v");
				float u = LUA->GetNumber(-2), v = LUA->GetNumber();
				LUA->Pop(2);
				mUvs.emplace_back(u, v);

				// Push back copy of ids
				mIds.push_back(id);

				// Pop MeshVertex
				LUA->Pop();

				// If this was the last vert in the tri, push back and validate normals, tangents, and binormals
				if (triIndex == 2U) {
					Triangle builtTri(
						Vector3{ tri[0].x, tri[0].y, tri[0].z },
						Vector3{ tri[1].x, tri[1].y, tri[1].z },
						Vector3{ tri[2].x, tri[2].y, tri[2].z }
					);

					// Check if triangle is invalid and remove vertices if so
					glm::vec3 geometricNormal{ builtTri.n[0], builtTri.n[1], builtTri.n[2] };
					if (!validVector(geometricNormal)) {
						mNormals.resize(mNormals.size() - 3);
						mTangents.resize(mTangents.size() - 3);
						mBinormals.resize(mBinormals.size() - 3);
						mUvs.resize(mUvs.size() - 3);
						mIds.resize(mIds.size() - 3);
						continue;
					}

					mTriangles.push_back(builtTri);

					// Maximum index of vertices
					size_t maxVert = mNormals.size() - 1;

					glm::vec3& n0 = mNormals[maxVert - 2U], & n1 = mNormals[maxVert - 1U], & n2 = mNormals[maxVert];
					if (!validVector(n0) || glm::dot(n0, geometricNormal) < 0.01f) n0 = geometricNormal;
					if (!validVector(n1) || glm::dot(n1, geometricNormal) < 0.01f) n1 = geometricNormal;
					if (!validVector(n2) || glm::dot(n2, geometricNormal) < 0.01f) n2 = geometricNormal;

					glm::vec3& t0 = mTangents[maxVert - 2U], & t1 = mTangents[maxVert - 1U], & t2 = mTangents[maxVert];
					if (
						!(
							validVector(t0) &&
							validVector(t1) &&
							validVector(t2)
						) ||
						fabsf(glm::dot(t0, n0)) > .9f ||
						fabsf(glm::dot(t1, n1)) > .9f ||
						fabsf(glm::dot(t2, n2)) > .9f
					) {
						glm::vec3 edge1 = tri[1] - tri[0];
						glm::vec3 edge2 = tri[2] - tri[0];

						glm::vec2 uv0 = mUvs[maxVert - 2U], uv1 = mUvs[maxVert - 1U], uv2 = mUvs[maxVert];
						glm::vec2 dUV1 = uv1 - uv0;
						glm::vec2 dUV2 = uv2 - uv0;

						float f = 1.f / (dUV1.x * dUV2.y - dUV2.x * dUV1.y);

						glm::vec3 geometricTangent{
							f * (dUV2.y * edge1.x - dUV1.y * edge2.x),
							f * (dUV2.y * edge1.y - dUV1.y * edge2.y),
							f * (dUV2.y * edge1.z - dUV1.y * edge2.z)
						};
						if (!validVector(geometricTangent)) {
							// Set the tangent to one of the edges as a guess on the plane (this will only be reached if the uvs overlap)
							geometricTangent = glm::normalize(edge1);
							mNormalMapBlacklist[mTriangles.size() - 1U] = true;
						}

						// Assign orthogonalised geometric tangent to vertices
						t0 = glm::normalize(geometricTangent - n0 * glm::dot(geometricTangent, n0));
						t1 = glm::normalize(geometricTangent - n1 * glm::dot(geometricTangent, n1));
						t2 = glm::normalize(geometricTangent - n2 * glm::dot(geometricTangent, n2));
					}

					glm::vec3& b0 = mBinormals[maxVert - 2U], & b1 = mBinormals[maxVert - 1U], & b2 = mBinormals[maxVert];
					if (!validVector(b0)) b0 = -glm::cross(n0, t0);
					if (!validVector(b1)) b1 = -glm::cross(n1, t1);
					if (!validVector(b2)) b2 = -glm::cross(n2, t2);
				}
			}

			// Pop triangle and mesh tables
			LUA->Pop(2);

			// Get textures
			std::string materialPath = "";
			LUA->GetField(-4, "GetMaterial");
			LUA->Push(-5);
			LUA->Call(1, 1);
			if (LUA->IsType(-1, Type::String)) materialPath = LUA->GetString();
			LUA->Pop();
			if (materialPath == "") {
				LUA->GetField(-4, "GetSubMaterial");
				LUA->Push(-5);
				LUA->PushNumber(meshIndex - 1U);
				LUA->Call(2, 1);
				if (LUA->IsType(-1, Type::String)) materialPath = LUA->GetString();
				LUA->Pop();

				if (materialPath == "") {
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

			LUA->GetField(-3, "Material");
			LUA->PushString(materialPath.c_str());
			LUA->Call(1, 1);
			if (!LUA->IsType(-1, Type::Material)) LUA->ThrowError("Invalid material on entity");

			mMaterials.push_back(materialPath);

			std::string baseTexture = getMaterialString(LUA, "$basetexture");
			std::string normalMap = getMaterialString(LUA, "$bumpmap");

			if (baseTexture.empty()) {
				baseTexture = MISSING_TEXTURE;
			}

			if (mTexCache.find(baseTexture) == mTexCache.end()) {
				VTFTexture* pTexture;
				if (!readTexture(baseTexture, mpFileSystem, &pTexture)) {
					pTexture = mTexCache[MISSING_TEXTURE];
				};
				mTexCache[baseTexture] = pTexture;
			}

			if (!normalMap.empty() && mTexCache.find(normalMap) == mTexCache.end()) {
				VTFTexture* pTexture;
				if (readTexture(normalMap, mpFileSystem, &pTexture))
					mTexCache[normalMap] = pTexture;
			}

			LUA->Pop();

			offset++;
		}

		LUA->Pop(3); // Pop meshes, util, and _G tables

		// Mark the entity as present in accel (for ent id verification later)
		LUA->PushBool(true);
		LUA->SetField(-2, "vistrace_mark");
		LUA->Pop(); // Pop entity
	}
	LUA->Pop(); // Pop entity table

	// Build BVH
	mAccel = BVH();
	bvh::SweepSahBuilder<BVH> builder(mAccel);
	auto [bboxes, centers] = bvh::compute_bounding_boxes_and_centers(mTriangles.data(), mTriangles.size());
	auto global_bbox = bvh::compute_bounding_boxes_union(bboxes.get(), mTriangles.size());
	builder.build(global_bbox, bboxes.get(), centers.get(), mTriangles.size());

	mpIntersector = new Intersector(mAccel, mTriangles.data());
	mpTraverser = new Traverser(mAccel);

	mAccelBuilt = true;
}

void AccelStruct::Traverse(ILuaBase* LUA)
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

	if (tMin < 0.f) LUA->ArgError(4, "tMin cannot be less than 0");
	if (tMax <= tMin) LUA->ArgError(5, "tMax must be greater than tMin");

	bool hitWorld = true;
	if (numArgs > 5 && !LUA->IsType(6, Type::Nil)) {
		LUA->CheckType(6, Type::Bool);
		hitWorld = LUA->GetBool(6);
	}

	bool hitWater = false;
	if (numArgs > 6 && !LUA->IsType(7, Type::Nil)) {
		LUA->CheckType(7, Type::Bool);
		hitWater = LUA->GetBool(7);
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
	if (hitWorld || hitWater) {
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

		LUA->PushNumber((hitWorld ? 16395 : 0 /* MASK_SOLID_BRUSHONLY */) | (hitWater ? 16432 : 0 /* MASK_WATER */));
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
	auto hit = mpTraverser->traverse(ray, *mpIntersector);
	if (hit) {
		auto intersection = hit->intersection;
		size_t vertIndex = hit->primitive_index * 3;
		std::pair<unsigned int, unsigned int> id = mIds[vertIndex];

		// If hitWorld is true, pop all the data from the stack for the world hit (world hit logic modifies the ray's tMax, so no need to compare distances if this managed to hit)
		if (hitWorld) LUA->Pop(3);

		// Entity
		glm::vec4 entColour{ 1, 1, 1, 1 };
		{
			LUA->PushSpecial(SPECIAL_GLOB);
			LUA->GetField(-1, "Entity");
			LUA->PushNumber(mIds.at(vertIndex).first);
			LUA->Call(1, 1);

			LUA->GetField(-1, "vistrace_mark");
			bool markedEnt = LUA->GetBool();
			LUA->Pop();

			if (!markedEnt) {
				LUA->Pop(); // Pop the invalid entity (not necessarily an invalid entity, but not the same as what was at that index when accel was built)
				LUA->GetField(-1, "Entity");
				LUA->PushNumber(-1);
				LUA->Call(1, 1);
			} else {
				LUA->GetField(-1, "GetColor");
				LUA->Push(-2);
				LUA->Call(1, 1);

				LUA->GetField(-1, "r");
				entColour[0] = LUA->GetNumber() / 255.f;
				LUA->Pop();

				LUA->GetField(-1, "g");
				entColour[1] = LUA->GetNumber() / 255.f;
				LUA->Pop();

				LUA->GetField(-1, "b");
				entColour[2] = LUA->GetNumber() / 255.f;
				LUA->Pop();

				LUA->GetField(-1, "a");
				entColour[3] = LUA->GetNumber() / 255.f;
				LUA->Pop(2);
			}

			LUA->SetField(1, "Entity");
			LUA->Pop(); // Pop _G
		}

		LUA->PushNumber(mIds.at(vertIndex).first);
		LUA->SetField(1, "EntIndex");

		LUA->PushNumber(intersection.t / tMax);
		LUA->SetField(1, "Fraction");

		LUA->PushBool(true);
		LUA->SetField(1, "Hit");

		LUA->PushBool(true);
		LUA->SetField(1, "HitNonWorld");

		// Calculate all information needed for texturing
		float u = intersection.u, v = intersection.v, w = (1.f - u - v);
		glm::vec2 texUV = w * mUvs[vertIndex] + u * mUvs[vertIndex + 1U] + v * mUvs[vertIndex + 2U];
		texUV -= glm::floor(texUV);

		{
			const Triangle& t = mTriangles[hit->primitive_index];
			Vector3 v = w * t.p0 + u * t.p1() + v * t.p2();
			LUA->PushVector(Vector{ v[0], v[1], v[2] });
			LUA->SetField(1, "HitPos");
		}

		glm::vec3 normal = w * mNormals.at(vertIndex) + u * mNormals.at(vertIndex + 1U) + v * mNormals.at(vertIndex + 2U);
		glm::vec3 tangent = w * mTangents.at(vertIndex) + u * mTangents.at(vertIndex + 1U) + v * mTangents.at(vertIndex + 2U);
		glm::vec3 binormal = w * mBinormals.at(vertIndex) + u * mBinormals.at(vertIndex + 1U) + v * mBinormals.at(vertIndex + 2U);

		normal = glm::normalize(normal);
		tangent = glm::normalize(tangent);
		binormal = glm::normalize(binormal);

		// Create shading data table
		LUA->PushSpecial(SPECIAL_GLOB);
		LUA->CreateTable();
		LUA->GetField(-2, "Material");
		LUA->PushString(mMaterials[mOffsets[id.first] + id.second].c_str());
		LUA->Call(1, 1);

		std::string baseTexturePath = getMaterialString(LUA, "$basetexture");
		std::string normalMapPath = getMaterialString(LUA, "$bumpmap");

		bool baseAlphaIsReflectivity = checkMaterialFlags(LUA, MaterialFlags::basealphaenvmapmask);

		LUA->SetField(-2, "Material");

		if (baseTexturePath.empty()) baseTexturePath = MISSING_TEXTURE;

		VTFTexture* pBaseTexture = mTexCache[baseTexturePath];
		VTFPixel colour = pBaseTexture->GetPixel(
			floor(texUV.x * pBaseTexture->GetWidth()),
			floor(texUV.y * pBaseTexture->GetHeight()),
			0
		);

		LUA->PushVector(Vector{ colour.r * entColour[0], colour.g * entColour[1], colour.b * entColour[2] });
		LUA->SetField(-2, "Albedo");

		LUA->PushNumber(colour.a * (baseAlphaIsReflectivity ? 1.f : entColour[3]));
		LUA->SetField(-2, "Alpha");

		float roughness = baseAlphaIsReflectivity ? colour.a : 1;

		if (
			!normalMapPath.empty() &&
			mNormalMapBlacklist.find(hit->primitive_index) == mNormalMapBlacklist.end() &&
			mTexCache.find(normalMapPath) != mTexCache.end()
		) {
			VTFTexture* pNormalTexture = mTexCache[normalMapPath];
			VTFPixel pixelNormal = pNormalTexture->GetPixel(
				floor(texUV.x * pNormalTexture->GetWidth()),
				floor(texUV.y * pNormalTexture->GetHeight()),
				0
			);

			if (!baseAlphaIsReflectivity)
				roughness = 1.f - pixelNormal.a;

			normal = glm::mat3{
				tangent[0],  tangent[1],  tangent[2],
				binormal[0], binormal[1], binormal[2],
				normal[0],   normal[1],   normal[2]
			} * (glm::vec3{ pixelNormal.r, pixelNormal.g, pixelNormal.b } * 2.f - 1.f);
			normal = glm::normalize(normal);


			tangent = glm::normalize(tangent - normal * glm::dot(tangent, normal));
			binormal = -glm::cross(normal, tangent);
		}

		LUA->PushNumber(roughness);
		LUA->SetField(-2, "Roughness");

		LUA->SetField(-3, "HitShader");
		LUA->Pop(); // Pop _G

		// Push normal, tangent, and binormal
		{
			Vector v;

			v.x = normal[0];
			v.y = normal[1];
			v.z = normal[2];
			LUA->PushVector(v);
			LUA->SetField(1, "HitNormal");

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
		LUA->CreateTable();
		LUA->PushNumber(texUV.x);
		LUA->SetField(-2, "u");
		LUA->PushNumber(texUV.y);
		LUA->SetField(-2, "v");
		LUA->SetField(1, "HitTexCoord");

		LUA->CreateTable();
		LUA->PushNumber(u);
		LUA->SetField(-2, "u");
		LUA->PushNumber(v);
		LUA->SetField(-2, "v");
		LUA->SetField(1, "HitBarycentric");

		// Push custom hidata values for submat id
		LUA->PushNumber(mIds.at(vertIndex).second);
		LUA->SetField(1, "SubmatIndex");

		// Push geometric normal of hit tri
		{
			Vector3 n = mTriangles.at(hit->primitive_index).n;
			normalise(n);

			Vector v;
			v.x = n[0];
			v.y = n[1];
			v.z = n[2];
			LUA->PushVector(v);
			LUA->SetField(1, "HitNormalGeometric");
		}
	}
}

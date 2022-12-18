#pragma once

#include "MDLParser.h"
#include "Primitives.h"

#include <string>

#include "glm/glm.hpp"

class Mesh;
class BodyGroup;
class Model;

class Mesh
{
private:
	bool mIsValid = false;

	int32_t mNumTris = 0U;
	Triangle* mpTris = nullptr;

	const BodyGroup* mpBodygroup = nullptr;

public:
	Mesh(const BodyGroup* pBodygroup, const MDLStructs::Model* pModel, const VTXStructs::Model* pVTXModel);
	~Mesh();

	bool IsValid() const;

	const BodyGroup* GetBodyGroup() const;
	const Model* GetModel() const;

	int32_t GetNumTriangles() const;
	const Triangle* GetTriangles() const;
};

class BodyGroup
{
private:
	bool mIsValid = false;

	int32_t mNumMeshes = 0U;
	Mesh** mpMeshes = nullptr;

	const Model* mpModel = nullptr;

public:
	BodyGroup(const Model* pModel, const MDLStructs::BodyPart* pBodypart, const VTXStructs::BodyPart* pVTXBodypart);
	~BodyGroup();

	bool IsValid() const;

	const Model* GetModel() const;

	int32_t GetNumMeshes() const;
	const Mesh* GetMesh(const int bodygroupValue) const;
};

class Model
{
private:
	struct MaterialPath
	{
		const char* directory = nullptr;
		const char* name = nullptr;
	};

	bool mIsValid = false;

	int32_t mNumBodygroups = 0U;
	BodyGroup** mpBodygroups = nullptr;

	MDL mMDL;

	MaterialPath* mpMaterialPaths = nullptr;
	glm::mat4* mpBindMatrices = nullptr;

public:
	Model(const std::string& path);
	~Model();

	bool IsValid() const;

	int32_t GetNumBodyGroups() const;
	const Mesh* GetMesh(const int bodygroup, const int bodygroupValue) const;

	const MDLStructs::Vector4D* GetTangent(const int i) const;
	const VVDStructs::Vertex* GetVertexData(const int i) const;

	int32_t GetNumBones() const;
	const MDLStructs::Bone* GetBone(const int i) const;

	glm::mat4 GetBindMatrix(const int i) const;

	int32_t GetNumSkinFamilies() const;
	int32_t GetNumMaterials() const;
	int16_t GetMaterialIdx(const int skin, const int materialId) const;
	std::string GetMaterial(const int materialId) const;
};

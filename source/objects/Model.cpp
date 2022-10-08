#include "Model.h"

#include "MDLParser.h"
#include "GMFS.h"

#include <new>
#include <string>

Mesh::Mesh(
	const BodyGroup* pBodygroup,
	const MDLStructs::Model* pModel, const VTXStructs::Model* pVTXModel
) : mpBodygroup(pBodygroup)
{
	const VTXStructs::ModelLoD* modelLod = pVTXModel->GetModelLoD(0);

	// Loop over all strips once to precompute the number of tris (this means we can use a single allocation)
	for (int mshIdx = 0; mshIdx < pModel->meshesCount; mshIdx++) {
		const MDLStructs::Mesh* mesh = pModel->GetMesh(mshIdx);
		const VTXStructs::Mesh* vtxMesh = modelLod->GetMesh(mshIdx);

		for (int grpIdx = 0; grpIdx < vtxMesh->numStripGroups; grpIdx++) {
			const VTXStructs::StripGroup* stripGroup = vtxMesh->GetStripGroup(grpIdx);

			for (int strIdx = 0; strIdx < stripGroup->numStrips; strIdx++) {
				const VTXStructs::Strip* strip = stripGroup->GetStrip(strIdx);

				if ((strip->flags & VTXEnums::StripFlags::IS_TRILIST) != VTXEnums::StripFlags::NONE) {
					mNumTris += strip->numIndices / 3;
				} else if ((strip->flags & VTXEnums::StripFlags::IS_TRISTRIP) != VTXEnums::StripFlags::NONE) {
					// nyi
				}
			}
		}
	}

	mpTris = static_cast<Triangle*>(malloc(mNumTris * sizeof(Triangle)));
	if (mpTris == nullptr) {
		mpTris = nullptr;
		mNumTris = 0;
		return;
	}

	for (int mshIdx = 0; mshIdx < pModel->meshesCount; mshIdx++) {
		const MDLStructs::Mesh* mesh = pModel->GetMesh(mshIdx);
		const VTXStructs::Mesh* vtxMesh = modelLod->GetMesh(mshIdx);

		for (int grpIdx = 0; grpIdx < vtxMesh->numStripGroups; grpIdx++) {
			const VTXStructs::StripGroup* stripGroup = vtxMesh->GetStripGroup(grpIdx);

			for (int strIdx = 0; strIdx < stripGroup->numStrips; strIdx++) {
				const VTXStructs::Strip* strip = stripGroup->GetStrip(strIdx);

				if ((strip->flags & VTXEnums::StripFlags::IS_TRILIST) != VTXEnums::StripFlags::NONE) {
					for (int i = strip->indexOffset; i < strip->numIndices + strip->indexOffset; i += 3) {
						uint16_t i1 = *stripGroup->GetIndex(i);
						uint16_t i2 = *stripGroup->GetIndex(i + 1);
						uint16_t i3 = *stripGroup->GetIndex(i + 2);

						const VTXStructs::Vertex* vtxVerts[3] = {
							stripGroup->GetVertex(i1),
							stripGroup->GetVertex(i2),
							stripGroup->GetVertex(i3)
						};

						const VVDStructs::Vertex* verts[3];
						const MDLStructs::Vector4D* tangents[3];
						for (int j = 0; j < 3; j++) {
							verts[j] = GetModel()->GetVertexData(mesh->GetVertexIndex(vtxVerts[j]));
							tangents[j] = GetModel()->GetTangent(mesh->GetTangentIndex(vtxVerts[j]));
						}

						Triangle tri(
							Vector3(verts[0]->pos.x, verts[0]->pos.y, verts[0]->pos.z),
							Vector3(verts[1]->pos.x, verts[1]->pos.y, verts[1]->pos.z),
							Vector3(verts[2]->pos.x, verts[2]->pos.y, verts[2]->pos.z),
							mesh->material,
							false
						);

						tri.alphas[0] = tri.alphas[1] = tri.alphas[2] = 0.f;

						for (int j = 0; j < 3; j++) {
							tri.normals[j] = glm::vec3(verts[j]->normal.x, verts[j]->normal.y, verts[j]->normal.z);
							tri.tangents[j] = glm::vec3(tangents[j]->x, tangents[j]->y, tangents[j]->z);

							tri.uvs[j] = glm::vec2(verts[j]->texCoord.x, verts[j]->texCoord.y);

							if (vtxVerts[j]->numBones > 0) {
								tri.numBones[j] = vtxVerts[j]->numBones;

								for (int boneIdx = 0; boneIdx < tri.numBones[j]; boneIdx++) {
									tri.weights[j][boneIdx] = verts[j]->boneWeights.weight[vtxVerts[j]->boneWeightIndex[boneIdx]];
									tri.boneIds[j][boneIdx] = verts[j]->boneWeights.bone[vtxVerts[j]->boneWeightIndex[boneIdx]];
								}
							} else {
								tri.numBones[j] = 1;
								tri.weights[j][0] = 1.f;
								tri.boneIds[j][0] = 0;
							}
						}

						mpTris[i / 3] = tri;
					}
				} else if ((strip->flags & VTXEnums::StripFlags::IS_TRISTRIP) != VTXEnums::StripFlags::NONE) {
					// nyi
				}
			}
		}
	}

	mIsValid = true;
}

Mesh::~Mesh()
{
	if (mpTris != nullptr) free(mpTris);
}

bool Mesh::IsValid() const { return mIsValid; }

const BodyGroup* Mesh::GetBodyGroup() const { return mpBodygroup; }
const Model* Mesh::GetModel() const { return mpBodygroup->GetModel(); }

int32_t Mesh::GetNumTriangles() const { return mNumTris; }
const Triangle* Mesh::GetTriangles() const { return mpTris; }

BodyGroup::BodyGroup(
	const Model* pModel,
	const MDLStructs::BodyPart* pBodypart, const VTXStructs::BodyPart* pVTXBodypart
) : mpModel(pModel)
{
	mNumMeshes = pBodypart->modelsCount;
	mpMeshes = new (std::nothrow) Mesh*[mNumMeshes];
	if (mpMeshes == nullptr) return;

	for (int i = 0; i < mNumMeshes; i++) {
		const MDLStructs::Model* model = pBodypart->GetModel(i);
		const VTXStructs::Model* vtxModel = pVTXBodypart->GetModel(i);

		mpMeshes[i] = new (std::nothrow) Mesh(this, model, vtxModel);
		if (mpMeshes[i] == nullptr || !mpMeshes[i]->IsValid()) return;
	}

	mIsValid = true;
}

BodyGroup::~BodyGroup()
{
	if (mpMeshes != nullptr) {
		for (int i = 0; i < mNumMeshes; i++) {
			if (mpMeshes[i] != nullptr) delete mpMeshes[i];
		}

		delete[] mpMeshes;
	}
}

bool BodyGroup::IsValid() const { return mIsValid; }

const Model* BodyGroup::GetModel() const { return mpModel; }

int32_t BodyGroup::GetNumMeshes() const { return mNumMeshes; }
const Mesh* BodyGroup::GetMesh(const int bodygroupValue) const
{
	return mpMeshes[bodygroupValue];
}

Model::Model(const std::string& path)
{
	std::string mdlPath = path + ".mdl";
	if (!FileSystem::Exists(mdlPath.c_str(), "GAME")) return;

	std::string vvdPath = path + ".vvd";
	if (!FileSystem::Exists(vvdPath.c_str(), "GAME")) return;

	std::string vtxPath = path + ".sw.vtx";
	if (!FileSystem::Exists(vtxPath.c_str(), "GAME")) {
		vtxPath = path + ".dx90.vtx";
		if (!FileSystem::Exists(vtxPath.c_str(), "GAME")) {
			vtxPath = path + ".dx80.vtx";
			if (!FileSystem::Exists(vtxPath.c_str(), "GAME")) return;
		}
	}

	FileHandle_t mdlFile = FileSystem::Open(mdlPath.c_str(), "rb", "GAME");
	FileHandle_t vvdfile = FileSystem::Open(vvdPath.c_str(), "rb", "GAME");
	FileHandle_t vtxFile = FileSystem::Open(vtxPath.c_str(), "rb", "GAME");

	uint32_t mdlSize = FileSystem::Size(mdlFile);
	uint32_t vvdSize = FileSystem::Size(vvdfile);
	uint32_t vtxSize = FileSystem::Size(vtxFile);

	uint8_t* mdlData = reinterpret_cast<uint8_t*>(malloc(mdlSize));
	uint8_t* vvdData = reinterpret_cast<uint8_t*>(malloc(vvdSize));
	uint8_t* vtxData = reinterpret_cast<uint8_t*>(malloc(vtxSize));

	if (mdlData == nullptr || vvdData == nullptr || vtxData == nullptr) return;

	FileSystem::Read(mdlData, mdlSize, mdlFile);
	FileSystem::Close(mdlFile);

	FileSystem::Read(vvdData, vvdSize, vvdfile);
	FileSystem::Close(vvdfile);

	FileSystem::Read(vtxData, vtxSize, vtxFile);
	FileSystem::Close(vtxFile);

	mMDL = MDL(
		mdlData, mdlSize,
		vvdData, vvdSize,
		vtxData, vtxSize
	);

	free(mdlData);
	free(vvdData);
	free(vtxData);

	if (!mMDL.IsValid()) return;

	mNumBodygroups = mMDL.GetNumBodyParts();
	mpBodygroups = new (std::nothrow) BodyGroup*[mNumBodygroups];
	if (mpBodygroups == nullptr) return;

	for (int bdyIdx = 0; bdyIdx < mMDL.GetNumBodyParts(); bdyIdx++) {
		const MDLStructs::BodyPart* bodypart;
		const VTXStructs::BodyPart* vtxBodypart;
		mMDL.GetBodyPart(bdyIdx, &bodypart, &vtxBodypart);

		mpBodygroups[bdyIdx] = new BodyGroup(this, bodypart, vtxBodypart);
		if (mpBodygroups[bdyIdx] == nullptr || !mpBodygroups[bdyIdx]->IsValid()) return;
	}

	mpBindMatrices = new glm::mat4[mMDL.GetNumBones()];
	for (int i = 0; i < mMDL.GetNumBones(); i++) {
		const MDLStructs::Matrix3x4& m = mMDL.GetBone(i)->poseToBone;
		
		glm::mat4x4 bind(
			m[0][0], m[1][0], m[2][0], 0,
			m[0][1], m[1][1], m[2][1], 0,
			m[0][2], m[1][2], m[2][2], 0,
			m[0][3], m[1][3], m[2][3], 1
		);

		/*glm::vec3 tmp(m[0][3], m[1][3], m[2][3]);

		bind[3][0] = -glm::dot(tmp, glm::vec3(bind[0]));
		bind[3][1] = -glm::dot(tmp, glm::vec3(bind[1]));
		bind[3][2] = -glm::dot(tmp, glm::vec3(bind[2]));*/

		mpBindMatrices[i] = bind;
	}

	mpMaterialPaths = new (std::nothrow) MaterialPath[mMDL.GetNumMaterials()];
	if (mpMaterialPaths == nullptr) return;

	for (int matIdx = 0; matIdx < mMDL.GetNumMaterials(); matIdx++) {
		MaterialPath matPath{ "", "" };
		matPath.name = mMDL.GetMaterialName(matIdx);

		for (int dirIdx = 0; dirIdx < mMDL.GetNumMaterialDirectories(); dirIdx++) {
			matPath.directory = mMDL.GetMaterialDirectory(dirIdx);

			std::string path = "materials/";
			path += matPath.directory;
			path += matPath.name;
			path += ".vmt";

			if (FileSystem::Exists(path.c_str(), "GAME")) break;
		}

		mpMaterialPaths[matIdx] = matPath;
	}

	mIsValid = true;
}

Model::~Model()
{
	if (mpBodygroups != nullptr) {
		for (int i = 0; i < mNumBodygroups; i++) {
			// If there's a nullptr then no more bodygroups should have been created
			if (mpBodygroups[i] == nullptr) break;

			delete mpBodygroups[i];
		}

		delete[] mpBodygroups;
	}

	if (mpMaterialPaths != nullptr) delete[] mpMaterialPaths;
	if (mpBindMatrices != nullptr) delete[] mpBindMatrices;
}

bool Model::IsValid() const { return mIsValid; }

int32_t Model::GetNumBodyGroups() const { return mNumBodygroups; }
const Mesh* Model::GetMesh(const int bodygroup, const int bodygroupValue) const
{
	if (bodygroup >= mNumBodygroups) return nullptr;
	
	const BodyGroup* pBodygroup = mpBodygroups[bodygroup];
	if (bodygroupValue >= pBodygroup->GetNumMeshes()) return nullptr;

	return pBodygroup->GetMesh(bodygroupValue);
}

const VVDStructs::Vector4D* Model::GetTangent(const int i) const
{
	if (i < 0 || i >= mMDL.GetNumVertices()) return nullptr;
	return mMDL.GetTangent(i);
}

const VVDStructs::Vertex* Model::GetVertexData(const int i) const {
	if (i < 0 || i >= mMDL.GetNumVertices()) return nullptr;
	return mMDL.GetVertex(i);
}

int32_t Model::GetNumBones() const { return mMDL.GetNumBones(); }
const MDLStructs::Bone* Model::GetBone(const int i) const
{
	if (i < 0 || i >= mMDL.GetNumBones()) return nullptr;
	return mMDL.GetBone(i);
}

glm::mat4 Model::GetBindMatrix(const int i) const
{
	if (i < 0 || i >= mMDL.GetNumBones()) return glm::mat4(
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	);

	return mpBindMatrices[i];
}

int32_t Model::GetNumSkinFamilies() const { return mMDL.GetNumSkinFamilies(); }
int32_t Model::GetNumMaterials() const { return mMDL.GetNumMaterials(); }

int16_t Model::GetMaterialIdx(const int skin, const int materialId) const
{
	if (
		skin < 0 || skin >= mMDL.GetNumSkinFamilies() ||
		materialId < 0 || materialId >= mMDL.GetNumMaterials()
	) return 0;
	return mMDL.GetMaterialIdx(skin, materialId);
}

std::string Model::GetMaterial(const int materialId) const
{
	if (materialId < 0 || materialId >= mMDL.GetNumMaterials()) return "";
	return std::string(mpMaterialPaths[materialId].directory) + mpMaterialPaths[materialId].name;
}

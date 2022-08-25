#include "VTFTexture.h"
#include "GMFS.h"

using namespace VisTrace;

int VTFTextureWrapper::id{ -1 };

VTFTextureWrapper::VTFTextureWrapper(const std::string& path)
{
	std::string texturePath = "materials/" + path + ".vtf";
	if (!FileSystem::Exists(texturePath.c_str(), "GAME")) return;
	FileHandle_t file = FileSystem::Open(texturePath.c_str(), "rb", "GAME");

	uint32_t filesize = FileSystem::Size(file);
	uint8_t* data = reinterpret_cast<uint8_t*>(malloc(filesize));
	if (data == nullptr) return;

	FileSystem::Read(data, filesize, file);
	FileSystem::Close(file);

	mpTex = new VTFTexture{ data, filesize };
	free(data);

	if (!mpTex->IsValid()) {
		delete mpTex;
		mpTex = nullptr;
		return;
	}

	mValid = true;
}

VTFTextureWrapper::~VTFTextureWrapper()
{
	if (mValid) delete mpTex;
	mValid = false;
	mpTex = nullptr;
}

bool VTFTextureWrapper::IsValid() const
{
	return mValid && mpTex != nullptr && mpTex->IsValid();
}

VTFTextureFormatInfo VTFTextureWrapper::GetFormat() const
{
	ImageFormatInfo pFormat = mpTex->GetFormat();
	return *reinterpret_cast<VTFTextureFormatInfo*>(&pFormat);
}
uint32_t VTFTextureWrapper::GetVersionMajor() const { return mpTex->GetVersionMajor(); }
uint32_t VTFTextureWrapper::GetVersionMinor() const { return mpTex->GetVersionMinor(); }

uint16_t VTFTextureWrapper::GetWidth(uint8_t mipLevel) const { return mpTex->GetWidth(mipLevel); }
uint16_t VTFTextureWrapper::GetHeight(uint8_t mipLevel) const { return mpTex->GetHeight(mipLevel); }
uint16_t VTFTextureWrapper::GetDepth(uint8_t mipLevel) const { return mpTex->GetDepth(mipLevel); }

uint8_t VTFTextureWrapper::GetFaces() const { return mpTex->GetFaces(); }

uint16_t VTFTextureWrapper::GetMIPLevels() const { return mpTex->GetMIPLevels(); }

uint16_t VTFTextureWrapper::GetFrames() const { return mpTex->GetFrames(); }
uint16_t VTFTextureWrapper::GetFirstFrame() const { return mpTex->GetFirstFrame(); }

Pixel VTFTextureWrapper::GetPixel(uint16_t x, uint16_t y, uint16_t z, uint8_t mipLevel, uint16_t frame, uint8_t face) const {
	VTFPixel p = mpTex->GetPixel(x, y, z, mipLevel, frame, face);
	return Pixel{ p.r, p.g, p.b, p.a };
}
Pixel VTFTextureWrapper::Sample(float u, float v, uint16_t z, float mipLevel, uint16_t frame, uint8_t face) const
{
	VTFPixel p = mpTex->Sample(u, v, z, mipLevel, frame, face);
	return Pixel{ p.r, p.g, p.b, p.a };
}

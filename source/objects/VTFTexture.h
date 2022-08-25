#pragma once

#include "VTFParser.h"
#include "vistrace/IVTFTexture.h"

#include <string>

class VTFTextureWrapper : public VisTrace::IVTFTexture
{
private:
	bool mValid = false;
	const VTFTexture* mpTex = nullptr;

public:
	static int id;

	VTFTextureWrapper(const std::string& path);
	~VTFTextureWrapper();

	bool IsValid() const;

	VisTrace::VTFTextureFormatInfo GetFormat() const;
	uint32_t GetVersionMajor() const;
	uint32_t GetVersionMinor() const;

	uint16_t GetWidth(uint8_t mipLevel = 0) const;
	uint16_t GetHeight(uint8_t mipLevel = 0) const;
	uint16_t GetDepth(uint8_t mipLevel = 0) const;

	uint8_t GetFaces() const;

	uint16_t GetMIPLevels() const;

	uint16_t GetFrames() const;
	uint16_t GetFirstFrame() const;

	VisTrace::Pixel GetPixel(uint16_t x, uint16_t y, uint16_t z, uint8_t mipLevel, uint16_t frame, uint8_t face) const;
	VisTrace::Pixel Sample(float u, float v, uint16_t z, float mipLevel, uint16_t frame, uint8_t face) const;
};

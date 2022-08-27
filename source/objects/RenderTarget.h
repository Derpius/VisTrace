#pragma once

#include <cstdint>
#include "vistrace/IRenderTarget.h"

struct RTFormatInfo
{
	uint8_t channels;
	size_t stride;
	bool hdr;
};

static const RTFormatInfo RT_FORMAT_INFO[static_cast<size_t>(VisTrace::RTFormat::Size)] = {
	{1, sizeof(uint8_t), false},
	{2, sizeof(uint8_t), false},
	{3, sizeof(uint8_t), false},
	{1, sizeof(float), true},
	{2, sizeof(float), true},
	{3, sizeof(float), true}
};

class RenderTarget : public VisTrace::IRenderTarget
{
private:
	uint8_t* mpBuffer = nullptr;

	VisTrace::RTFormat mFormat;
	size_t mChannelSize, mPixelSize, mSize;

	uint16_t mWidth = 0, mHeight = 0;
	uint8_t mMips = 0;
	size_t mMipOffsets[VisTrace::MAX_MIPS];

	VisTrace::Pixel SampleBilinear(float u, float v, uint8_t mip) const;

public:
	static int id;

	RenderTarget() {}
	RenderTarget(uint16_t width, uint16_t height, VisTrace::RTFormat format, uint8_t mips = 1);
	~RenderTarget();

	bool Resize(uint16_t width, uint16_t height, uint8_t mips = 1);

	bool IsValid() const;
	uint16_t GetWidth() const;
	uint16_t GetHeight() const;
	uint8_t GetMIPs() const;
	VisTrace::RTFormat GetFormat() const;

	uint8_t* GetRawData(uint8_t mip = 0);
	size_t GetPixelSize() const;
	size_t GetSize() const;

	VisTrace::Pixel GetPixel(uint16_t x, uint16_t y, uint8_t mip = 0) const;
	void SetPixel(uint16_t x, uint16_t y, const VisTrace::Pixel& pixel, uint8_t mip = 0);

	void GenerateMIPs();

	bool Save(const char* filename, uint8_t mip = 0) const;
	bool Load(const char* filename, bool createMips = false);
};

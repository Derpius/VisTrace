#pragma once

#include <cstdint>
#include "IRenderTarget.h"

namespace RT
{
	class Texture : public ITexture
	{
	private:
		uint8_t* mpBuffer = nullptr;

		Format mFormat;
		size_t mChannelSize, mPixelSize, mSize;

		uint16_t mWidth = 0, mHeight = 0;
		uint8_t mMips = 0;
		size_t mMipOffsets[MAX_MIPS];

		Pixel SampleBilinear(float u, float v, uint8_t mip) const;

	public:
		static int id;

		Texture() {}
		Texture(uint16_t width, uint16_t height, Format format, uint8_t mips = 1);
		~Texture();

		bool Resize(uint16_t width, uint16_t height, uint8_t mips = 1);

		bool IsValid() const;
		uint16_t GetWidth() const;
		uint16_t GetHeight() const;
		uint8_t GetMIPs() const;
		Format GetFormat() const;

		uint8_t* GetRawData(uint8_t mip = 0);
		size_t GetPixelSize() const;
		size_t GetSize() const;

		Pixel GetPixel(uint16_t x, uint16_t y, uint8_t mip = 0) const;
		void SetPixel(uint16_t x, uint16_t y, const Pixel& pixel, uint8_t mip = 0);

		void GenerateMIPs();
	};
}

#pragma once

#include <cstdint>
#include "IRenderTarget.h"

namespace RT
{
	class Texture : public ITexture
	{
	public:
		static int id;

		Texture(uint16_t width, uint16_t height, Format format);
		~Texture();

		bool Resize(uint16_t width, uint16_t height);

		bool IsValid() const;
		uint16_t GetWidth() const;
		uint16_t GetHeight() const;
		Format GetFormat() const;

		uint8_t* GetRawData();
		size_t GetPixelSize() const;
		size_t GetSize() const;

		Pixel GetPixel(uint16_t x, uint16_t y) const;
		void SetPixel(uint16_t x, uint16_t y, const Pixel& pixel);
	};
}

#pragma once

#include <cstdint>

namespace RT
{
	struct Pixel
	{
		float r = 0, g = 0, b = 0, a = 0;

		float& operator[](size_t i)
		{
			switch (i) {
			case 0:
				return r;
			case 1:
				return g;
			case 2:
				return b;
			case 3:
				return a;
			default:
				return r;
			}
		}
	};

	enum class Format : uint8_t
	{
		R8,
		RG88,
		RGB888,
		RGBFFF,
		Size,

		Albedo = RGBFFF,
		Normal = RGBFFF
	};

	static const uint8_t CHANNELS[static_cast<size_t>(Format::Size)] = {
		1,
		2,
		3,
		3
	};

	static const size_t STRIDES[static_cast<size_t>(Format::Size)] = {
		sizeof(uint8_t),
		sizeof(uint8_t),
		sizeof(uint8_t),
		sizeof(float)
	};

	class ITexture
	{
	protected:
		Format mFormat;
		size_t mChannelSize, mPixelSize;
		uint8_t* pBuffer = nullptr;
		uint16_t mWidth = 0, mHeight = 0;

	public:
		ITexture() {};
		virtual ~ITexture() {};

		virtual bool Resize(uint16_t width, uint16_t height) = 0;

		virtual bool IsValid() const = 0;
		virtual uint16_t GetWidth() const = 0;
		virtual uint16_t GetHeight() const = 0;
		virtual Format GetFormat() const = 0;

		virtual uint8_t* GetRawData() = 0;
		virtual size_t GetPixelSize() const = 0;
		virtual size_t GetSize() const = 0;

		virtual Pixel GetPixel(uint16_t x, uint16_t y) const = 0;
		virtual void SetPixel(uint16_t x, uint16_t y, const Pixel& pixel) = 0;
	};
}

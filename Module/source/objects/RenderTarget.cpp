#include "RenderTarget.h"

using namespace RT;

int Texture::id{ -1 };

Texture::Texture(uint16_t width, uint16_t height, Format format)
	: mFormat(format),
	mChannelSize(STRIDES[static_cast<uint8_t>(format)]),
	mPixelSize(STRIDES[static_cast<uint8_t>(format)] * CHANNELS[static_cast<uint8_t>(format)])
{
	Resize(width, height);
}

Texture::~Texture()
{
	if (pBuffer != nullptr) {
		free(pBuffer);
		pBuffer = nullptr;
		mWidth = mHeight = 0;
	}
}

bool Texture::Resize(uint16_t width, uint16_t height)
{
	mWidth = width;
	mHeight = height;
	pBuffer = static_cast<uint8_t*>(realloc(pBuffer, mPixelSize * mWidth * mHeight));

	return IsValid();
}

bool Texture::IsValid() const {
	return pBuffer != nullptr && mWidth > 0 && mHeight > 0;
}

uint16_t Texture::GetWidth() const { return mWidth; }
uint16_t Texture::GetHeight() const { return mHeight; }
Format Texture::GetFormat() const { return mFormat; }
uint8_t* Texture::GetRawData() { return reinterpret_cast<uint8_t*>(pBuffer); }

Pixel Texture::GetPixel(uint16_t x, uint16_t y) const
{
	if (!IsValid()) return Pixel{};

	const size_t offset = (y * mWidth + x) * mPixelSize;
	if (offset >= mPixelSize * mWidth * mHeight) return Pixel{};

	switch (mFormat) {
	case Format::R8:
		return Pixel{
			static_cast<float>(pBuffer[offset])
		};
	case Format::RG88:
		return Pixel{
			static_cast<float>(pBuffer[offset]),
			static_cast<float>(pBuffer[offset + mChannelSize])
		};
	case Format::RGB888:
		return Pixel{
			static_cast<float>(pBuffer[offset]),
			static_cast<float>(pBuffer[offset + mChannelSize]),
			static_cast<float>(pBuffer[offset + mChannelSize * 2])
		};
	case Format::RGBFFF:
		return Pixel{
			*reinterpret_cast<float*>(pBuffer[offset]),
			*reinterpret_cast<float*>(pBuffer[offset + mChannelSize]),
			*reinterpret_cast<float*>(pBuffer[offset + mChannelSize * 2])
		};
	default:
		return Pixel{};
	}
}

void Texture::SetPixel(uint16_t x, uint16_t y, const Pixel& pixel)
{
	if (!IsValid()) return;

	const size_t offset = (y * mWidth + x) * mPixelSize;
	if (offset >= mPixelSize * mWidth * mHeight) return;

	switch (mFormat) {
	case Format::R8:
		pBuffer[offset]                    = pixel.r;
	case Format::RG88:
		pBuffer[offset]                    = pixel.r;
		pBuffer[offset + mChannelSize]     = pixel.g;
	case Format::RGB888:
		pBuffer[offset]                    = pixel.r;
		pBuffer[offset + mChannelSize]     = pixel.g;
		pBuffer[offset + mChannelSize * 2] = pixel.b;
	case Format::RGBFFF:
		*reinterpret_cast<float*>(pBuffer + offset)                    = pixel.r;
		*reinterpret_cast<float*>(pBuffer + offset + mChannelSize)     = pixel.g;
		*reinterpret_cast<float*>(pBuffer + offset + mChannelSize * 2) = pixel.b;
	default:
		return;
	}
}

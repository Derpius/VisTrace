#include "RenderTarget.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace RT;

int Texture::id{ -1 };

Texture::Texture(uint16_t width, uint16_t height, Format format)
{
	mFormat = format;
	mChannelSize = STRIDES[static_cast<uint8_t>(mFormat)];
	mPixelSize = mChannelSize * CHANNELS[static_cast<uint8_t>(mFormat)];
	Resize(width, height);
}

Texture::~Texture()
{
	if (pBuffer != nullptr) {
		free(pBuffer);
		pBuffer = nullptr;
	}
	mWidth = mHeight = 0;
}

bool Texture::Resize(uint16_t width, uint16_t height)
{
	printf("\n\n\n%ux%u\n\n\n", width, height);

	if (width * height == 0) {
		if (pBuffer != nullptr) {
			free(pBuffer);
			pBuffer = nullptr;
		}
		mWidth = mHeight = 0;
	} else {
		mWidth = width;
		mHeight = height;
		pBuffer = static_cast<uint8_t*>(realloc(pBuffer, mPixelSize * mWidth * mHeight));
	}

	if (IsValid()) {
		memset(pBuffer, 0, mPixelSize * mWidth * mHeight);
		return true;
	}
	return false;
}

bool Texture::IsValid() const {
	return pBuffer != nullptr && mWidth > 0 && mHeight > 0;
}

uint16_t Texture::GetWidth() const { return mWidth; }
uint16_t Texture::GetHeight() const { return mHeight; }
Format Texture::GetFormat() const { return mFormat; }

uint8_t* Texture::GetRawData() { return pBuffer; }
size_t Texture::GetPixelSize() const { return mPixelSize; }
size_t Texture::GetSize() const { return mPixelSize * mWidth * mHeight; }

Pixel Texture::GetPixel(uint16_t x, uint16_t y) const
{
	if (!IsValid()) return Pixel{};

	const size_t offset = (y * mWidth + x) * mPixelSize;
	if (offset >= mPixelSize * mWidth * mHeight) return Pixel{};

	switch (mFormat) {
	case Format::R8:
		return Pixel{
			static_cast<float>(pBuffer[offset]) / 255.f
		};
	case Format::RG88:
		return Pixel{
			static_cast<float>(pBuffer[offset]) / 255.f,
			static_cast<float>(pBuffer[offset + mChannelSize]) / 255.f
		};
	case Format::RGB888:
		return Pixel{
			static_cast<float>(pBuffer[offset]) / 255.f,
			static_cast<float>(pBuffer[offset + mChannelSize]) / 255.f,
			static_cast<float>(pBuffer[offset + mChannelSize * 2]) / 255.f
		};
	case Format::RGBFFF:
		return Pixel{
			*reinterpret_cast<float*>(pBuffer + offset),
			*reinterpret_cast<float*>(pBuffer + offset + mChannelSize),
			*reinterpret_cast<float*>(pBuffer + offset + mChannelSize * 2)
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
	case Format::RGB888:
		pBuffer[offset + mChannelSize * 2] = std::clamp(pixel.b * 255.f, 0.f, 255.f);
	case Format::RG88:
		pBuffer[offset + mChannelSize]     = std::clamp(pixel.g * 255.f, 0.f, 255.f);
	case Format::R8:
		pBuffer[offset]                    = std::clamp(pixel.r * 255.f, 0.f, 255.f);
		return;
	case Format::RGBFFF:
		*reinterpret_cast<float*>(pBuffer + offset)                    = pixel.r;
		*reinterpret_cast<float*>(pBuffer + offset + mChannelSize)     = pixel.g;
		*reinterpret_cast<float*>(pBuffer + offset + mChannelSize * 2) = pixel.b;
		return;
	default:
		return;
	}
}

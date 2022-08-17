#include "RenderTarget.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace VisTrace;

int RenderTarget::id{ -1 };

RenderTarget::RenderTarget(uint16_t width, uint16_t height, RTFormat format, uint8_t mips)
{
	mFormat = format;
	mChannelSize = STRIDES[static_cast<uint8_t>(mFormat)];
	mPixelSize = mChannelSize * CHANNELS[static_cast<uint8_t>(mFormat)];
	Resize(width, height, mips);
}

RenderTarget::~RenderTarget()
{
	if (mpBuffer != nullptr) {
		free(mpBuffer);
		mpBuffer = nullptr;
	}
	mWidth = mHeight = 0;
}

bool RenderTarget::Resize(uint16_t width, uint16_t height, uint8_t mips)
{
	mSize = mPixelSize * width * height;

	if (mSize == 0) {
		if (mpBuffer != nullptr) {
			free(mpBuffer);
			mpBuffer = nullptr;
		}
		mWidth = mHeight = mMips = 0;
	} else {
		mWidth = width;
		mHeight = height;
		mMips = mips;
		mMipOffsets[0] = 0;

		for (uint8_t mip = 1; mip < mips; mip++) {
			width >>= 1;
			height >>= 1;
			if (width < 1) width = 1;
			if (height < 1) height = 1;

			mMipOffsets[mip] = mSize; // Set this before increasing size as the size is already calculated for the *previous* mip
			mSize += mPixelSize * width * height;
		}

		mpBuffer = static_cast<uint8_t*>(realloc(mpBuffer, mSize));
		if (mpBuffer != nullptr) {
			memset(mpBuffer, 0, mSize);
			return true;
		}
	}

	return false;
}

bool RenderTarget::IsValid() const {
	return mpBuffer != nullptr && mSize > 0;
}

uint16_t RenderTarget::GetWidth() const { return mWidth; }
uint16_t RenderTarget::GetHeight() const { return mHeight; }
uint8_t RenderTarget::GetMIPs() const { return mMips; }
RTFormat RenderTarget::GetFormat() const { return mFormat; }

uint8_t* RenderTarget::GetRawData(uint8_t mip) {
	if (!IsValid() || mip >= mMips) return nullptr;
	return mpBuffer + mMipOffsets[mip];
}

size_t RenderTarget::GetPixelSize() const { return mPixelSize; }
size_t RenderTarget::GetSize() const { return mSize; }

Pixel RenderTarget::GetPixel(uint16_t x, uint16_t y, uint8_t mip) const
{
	if (!IsValid() || mip >= mMips) return Pixel{};

	uint16_t width = mWidth >> mip, height = mHeight >> mip;
	if (width < 1) width = 1;
	if (height < 1) height = 1;
	if (x >= width || y >= height) return Pixel{};
	size_t offset = mMipOffsets[mip];

	offset += (y * static_cast<size_t>(width) + x) * mPixelSize;
	if (offset >= mSize) return Pixel{};

	Pixel pixel{};

	switch (mFormat) {
	case RTFormat::RGB888:
		pixel.b = static_cast<float>(mpBuffer[offset + 2]) / 255.f;
	case RTFormat::RG88:
		pixel.g = static_cast<float>(mpBuffer[offset + 1]) / 255.f;
	case RTFormat::R8:
		pixel.r = static_cast<float>(mpBuffer[offset]) / 255.f;
		return pixel;

	case RTFormat::RGBFFF:
		pixel.b = *reinterpret_cast<float*>(mpBuffer + offset + mChannelSize * 2);
	case RTFormat::RGFF:
		pixel.g = *reinterpret_cast<float*>(mpBuffer + offset + mChannelSize);
	case RTFormat::RF:
		pixel.r = *reinterpret_cast<float*>(mpBuffer + offset);
		return pixel;

	default:
		return pixel;
	}
}

void RenderTarget::SetPixel(uint16_t x, uint16_t y, const Pixel& pixel, uint8_t mip)
{
	if (!IsValid() || mip >= mMips) return;

	uint16_t width = mWidth >> mip, height = mHeight >> mip;
	if (width < 1) width = 1;
	if (height < 1) height = 1;
	if (x >= width || y >= height) return;
	size_t offset = mMipOffsets[mip];

	offset += (y * static_cast<size_t>(width) + x) * mPixelSize;
	if (offset >= mSize) return;

	switch (mFormat) {
	case RTFormat::RGB888:
		mpBuffer[offset + 2] = std::clamp(pixel.b * 255.f, 0.f, 255.f);
	case RTFormat::RG88:
		mpBuffer[offset + 1] = std::clamp(pixel.g * 255.f, 0.f, 255.f);
	case RTFormat::R8:
		mpBuffer[offset] = std::clamp(pixel.r * 255.f, 0.f, 255.f);
		return;

	case RTFormat::RGBFFF:
		*reinterpret_cast<float*>(mpBuffer + offset + mChannelSize * 2) = pixel.b;
	case RTFormat::RGFF:
		*reinterpret_cast<float*>(mpBuffer + offset + mChannelSize) = pixel.g;
	case RTFormat::RF:
		*reinterpret_cast<float*>(mpBuffer + offset) = pixel.r;
		return;

	default:
		return;
	}
}

inline int intmod(int a, int b)
{
	return (a % b + b) % b;
}
Pixel RenderTarget::SampleBilinear(float u, float v, uint8_t mip) const
{
	uint32_t offset = mMipOffsets[mip];

	uint16_t width = mWidth >> mip;
	uint16_t height = mHeight >> mip;
	if (width < 1) width = 1;
	if (height < 1) height = 1;

	Pixel filtered{};

	// Remap to 0-1
	u -= floorf(u);
	v -= floorf(v);

	// Remap to pixel centres
	u = u * width - 0.5f;
	v = v * height - 0.5f;

	// Floor to nearest pixel
	int x = floorf(u);
	int y = floorf(v);

	// Calculate fractional coordinate and inverse
	float uFract = u - x;
	float vFract = v - y;
	float uFractInv = 1.f - uFract;
	float vFractInv = 1.f - vFract;

	Pixel corners[2][2];
	for (int xOff = 0; xOff < 2; xOff++) {
		for (int yOff = 0; yOff < 2; yOff++) {
			int xCorner = x + xOff, yCorner = y + yOff;
			xCorner = intmod(xCorner, width);
			yCorner = intmod(yCorner, height);

			corners[xOff][yOff] = GetPixel(xCorner, yCorner, mip);
		}
	}

	return Pixel{
		(corners[0][0].r * uFractInv + corners[1][0].r * uFract) * vFractInv +
		(corners[0][1].r * uFractInv + corners[1][1].r * uFract) * vFract,

		(corners[0][0].g * uFractInv + corners[1][0].g * uFract) * vFractInv +
		(corners[0][1].g * uFractInv + corners[1][1].g * uFract) * vFract,

		(corners[0][0].b * uFractInv + corners[1][0].b * uFract) * vFractInv +
		(corners[0][1].b * uFractInv + corners[1][1].b * uFract) * vFract,

		(corners[0][0].a * uFractInv + corners[1][0].a * uFract) * vFractInv +
		(corners[0][1].a * uFractInv + corners[1][1].a * uFract) * vFract,
	};
}

void RenderTarget::GenerateMIPs()
{
	if (!IsValid()) return;

	uint16_t width = mWidth, height = mHeight;

	// For each mip level after the raw image
	for (uint8_t mip = 1; mip < mMips; mip++) {
		width >>= 1;
		height >>= 1;
		if (width < 1) width = 1;
		if (height < 1) height = 1;

		// For each pixel in the mip
		#pragma omp parallel for collapse(2)
		for (uint16_t y = 0; y < height; y++) {
			for (uint16_t x = 0; x < width; x++) {
				// Calculate uv offset to centre of pixel (this way when we sample the higher mip we get a proper average)
				float u = (static_cast<float>(x) + 0.5f) / width;
				float v = (static_cast<float>(y) + 0.5f) / height;

				// Sample the higher mip with filtering and set as the value for the pixel in this mip
				Pixel sample = SampleBilinear(u, v, mip - 1);
				SetPixel(x, y, sample, mip);
			}
		}
	}
}

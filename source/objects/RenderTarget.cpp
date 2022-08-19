#include "RenderTarget.h"
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>

using namespace VisTrace;
namespace fs = std::filesystem;

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

bool RenderTarget::Load(const char* filepath, bool generateMips, bool scaleToRT)
{
	if (!IsValid()) return false;

	int imageWidth = 0;
	int imageHeight = 0;
	int channels = CHANNELS[static_cast<uint8_t>(GetFormat())];
	size_t stride = STRIDES[static_cast<uint8_t>(GetFormat())];
	
	uint8_t* rtData = GetRawData();
	const bool isFloat = stride == sizeof(float);

	if (isFloat) {
		float* imageData = stbi_loadf(filepath, &imageWidth, &imageHeight, nullptr, channels);
		if (imageData) {
			// Check if we require a resize to the RT's dimensions.
			if (scaleToRT) {
				uint16_t width = GetWidth();
				uint16_t height = GetHeight();

				// We can do a cool trick and skip any mediums and directly have the resized image be written into the RT's image buffer.
				int resized = stbir_resize_float(imageData, imageWidth, imageHeight, channels * stride * imageWidth, reinterpret_cast<float*>(rtData), width, height, channels * stride * width, channels);
				if (!resized) {
					stbi_image_free(imageData);
					return false;
				}

				stbi_image_free(imageData);
			}
			else {
				// Resize the RT to fit the image.
				bool resized = Resize(imageWidth, imageHeight);
				if (!resized) {
					stbi_image_free(imageData);
					return false;
				}

				memcpy(reinterpret_cast<void*>(GetRawData()), reinterpret_cast<void*>(imageData), stride * imageWidth * imageHeight * channels);
				stbi_image_free(imageData);
			}
		}
		else {
			return false;
		}
	}
	else {
		stbi_uc* imageData = stbi_load(filepath, &imageWidth, &imageHeight, nullptr, channels);
		if (imageData) {
			// Check if we require a resize to the RT's dimensions.
			if (scaleToRT) {
				uint16_t width = GetWidth();
				uint16_t height = GetHeight();

				// We can do a cool trick and skip any mediums and directly have the resized image be written into the RT's image buffer.
				int resized = stbir_resize_uint8(imageData, imageWidth, imageHeight, channels * stride * imageWidth, reinterpret_cast<uint8_t*>(rtData), width, height, channels * stride * width, channels);
				if (!resized) {
					stbi_image_free(imageData);
					return false;
				}

				stbi_image_free(imageData);
			}
			else {
				// Resize the RT to fit the image.
				bool resized = Resize(imageWidth, imageHeight);
				if (!resized) {
					stbi_image_free(imageData);
					return false;
				}

				memcpy(reinterpret_cast<void*>(GetRawData()), reinterpret_cast<void*>(imageData), stride * imageWidth * imageHeight * channels);
				stbi_image_free(imageData);
			}
		}
		else {
			return false;
		}
	}

	// MIP generation works on any type of buffer.
	if (generateMips) {
		GenerateMIPs();
	}

	return true;
}

bool RenderTarget::Save(const char* filename, uint8_t mip)
{
	int channels = CHANNELS[static_cast<uint8_t>(GetFormat())];
	if (GetMIPs() <= mip) {
		return false;
	}

	fs::path filepath(filename);
	filepath = filepath.make_preferred();

	fs::path workingDir = fs::current_path();

	workingDir += "/garrysmod/data";
	workingDir = workingDir.make_preferred();
	workingDir = workingDir.lexically_normal();

	const bool isFloat = mChannelSize == sizeof(float);
	std::string extension = isFloat ? "hdr" : "png";

	bool createSubdirectories = filepath.has_parent_path(); // Returns true if the filename is like "renders/output.png"

	if (filepath.has_extension()) {
		fs::path fileExt = filepath.extension();
		std::string ext = fileExt.string();
		ext = ext.substr(1); // Extension returns the period along with the actual extension.

		if (ext == "hdr" || ext == "png" || ext == "jpg" || ext == "bmp") {
			extension = ext; // Change the default to the requested one
		}
		else {
			// Append the default extension.
			// Great for things like: "frame.001"
			filepath += "." + extension;
		}
	}

	fs::path finalPath = workingDir / filepath;
	finalPath = finalPath.lexically_normal(); // Absolute.

	// We can assure that the user did not go out of bounds by checking that the root is equal to the working directory.
	std::string finalPathString = finalPath.string();
	std::string workingDirString = workingDir.string();

	std::string root = finalPathString.substr(0, workingDirString.length());
	if (root != workingDirString) {
		return false; // User exited the data directory.
	}
	
	if (createSubdirectories) {
		fs::create_directories(finalPath.parent_path());
	}

	const char* rawFilepath = finalPath.string().c_str();
	
	if (extension == "hdr") {
		if (!stbi_write_hdr(rawFilepath, GetWidth(), GetHeight(), channels, reinterpret_cast<const float*>(GetRawData(mip)))) {
			return false;
		}
	}
	else if (extension == "png") {
		if (!stbi_write_png(rawFilepath, GetWidth(), GetHeight(), channels, reinterpret_cast<const void*>(GetRawData(mip)), mPixelSize * GetWidth())) {
			return false;
		}
	}
	else if (extension == "jpg") {
		if (!stbi_write_jpg(rawFilepath, GetWidth(), GetHeight(), channels, reinterpret_cast<const void*>(GetRawData(mip)), 50)) {
			return false;
		}
	}
	else if (extension == "bmp") {
		if (!stbi_write_bmp(rawFilepath, GetWidth(), GetHeight(), channels, reinterpret_cast<const void*>(GetRawData(mip)))) {
			return false;
		}
	}
	else {
		return false;
	}

	return true;
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

#pragma once

#include "vistrace/Structs.h"

#include <cstdint>

namespace VisTrace
{
	constexpr uint8_t MAX_MIPS = 32;

	enum class RTFormat : uint8_t
	{
		R8,
		RG88,
		RGB888,
		RF,
		RGFF,
		RGBFFF,
		Size,

		Albedo = RGBFFF,
		Normal = RGBFFF
	};

	class IRenderTarget
	{
	public:
		IRenderTarget() {};
		virtual ~IRenderTarget() {};

		virtual bool Resize(uint16_t width, uint16_t height, uint8_t mips = 1) = 0;

		virtual bool IsValid() const = 0;
		virtual uint16_t GetWidth(uint8_t mip = 0) const = 0;
		virtual uint16_t GetHeight(uint8_t mip = 0) const = 0;
		virtual uint8_t GetMIPs() const = 0;
		virtual RTFormat GetFormat() const = 0;

		virtual uint8_t* GetRawData(uint8_t mip = 0) = 0;
		virtual size_t GetPixelSize() const = 0;
		virtual size_t GetSize() const = 0;

		virtual Pixel GetPixel(uint16_t x, uint16_t y, uint8_t mip = 0) const = 0;
		virtual void SetPixel(uint16_t x, uint16_t y, const Pixel& pixel, uint8_t mip = 0) = 0;

		virtual void GenerateMIPs() = 0;
		virtual IRenderTarget* Clone() = 0;

		virtual bool Save(const char* filename, uint8_t mip = 0) const = 0;
		virtual bool Load(const char* filename, bool createMips = false) = 0;
	};
}

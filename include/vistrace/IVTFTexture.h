#pragma once

#include "vistrace/Structs.h"

#include <cstdint>

namespace VisTrace
{
	struct VTFTextureFormatInfo
	{
		const char* name;
		uint32_t bitsPerPixel;
		uint32_t bytesPerPixel;
		uint32_t redBitsPerPixel;
		uint32_t greenBitsPerPixel;
		uint32_t blueBitsPerPixel;
		uint32_t alphaBitsPerPixel;
		bool isCompressed;
		bool isSupported;
	};

	class IVTFTexture
	{
	public:
		IVTFTexture() = default;
		virtual ~IVTFTexture() {};

		/// <summary>
		/// Returns whether or not the image is valid
		/// </summary>
		/// <returns>True if the header and image data were read successfully</returns>
		virtual bool IsValid() const = 0;

		virtual VTFTextureFormatInfo GetFormat() const = 0;
		virtual uint32_t GetVersionMajor() const = 0;
		virtual uint32_t GetVersionMinor() const = 0;

		/// <summary>
		/// Gets the width of the image at the specified mip level
		/// </summary>
		/// <param name="mipLevel">Mipmap to get the width of (default: 0)</param>
		/// <returns>Width of the image in pixels</returns>
		virtual uint16_t GetWidth(uint8_t mipLevel = 0) const = 0;

		/// <summary>
		/// Gets the height of the image at the specified mip level
		/// </summary>
		/// <param name="mipLevel">Mipmap to get the height of (default: 0)</param>
		/// <returns>Height of the image in pixels</returns>
		virtual uint16_t GetHeight(uint8_t mipLevel = 0) const = 0;

		/// <summary>
		/// Gets the depth of the image at the specified mip level
		/// </summary>
		/// <param name="mipLevel">Mipmap to get the depth of (default: 0)</param>
		/// <returns>Depth of the image in pixels</returns>
		virtual uint16_t GetDepth(uint8_t mipLevel = 0) const = 0;

		/// <summary>
		/// Gets the number of faces in an image
		/// </summary>
		/// <returns>6/7 for envmaps depending on version, 1 for anything else</returns>
		virtual uint8_t GetFaces() const = 0;

		virtual uint16_t GetMIPLevels() const = 0;

		virtual uint16_t GetFrames() const = 0;
		virtual uint16_t GetFirstFrame() const = 0;

		/// <summary>
		/// Gets a pixel from the image at the specified coordinate, MIP level, frame, and face
		/// </summary>
		/// <param name="x">Coordinate of the pixel on the x axis</param>
		/// <param name="y">Coordinate of the pixel on the y axis</param>
		/// <param name="z">Coordinate of the pixel on the z axis (volumetric textures only)</param>
		/// <param name="mipLevel">MIP level to read (automatically transforms above coordinates)</param>
		/// <param name="frame">Frame of the image (animated textures only)</param>
		/// <param name="face">Face of the image (envmaps only)</param>
		/// <returns>Pixel struct with the pixel data</returns>
		virtual Pixel GetPixel(uint16_t x, uint16_t y, uint16_t z, uint8_t mipLevel, uint16_t frame, uint8_t face) const = 0;

		/// <summary>
		/// Gets a pixel from the image at the specified coordinate, MIP level, and frame of an animated 2D texture
		/// </summary>
		/// <param name="x">Coordinate of the pixel on the x axis</param>
		/// <param name="y">Coordinate of the pixel on the y axis</param>
		/// <param name="mipLevel">MIP level to read (automatically transforms above coordinates)</param>
		/// <param name="frame">Frame of the image</param>
		/// <returns>Pixel struct with the pixel data</returns>
		inline Pixel GetPixel(uint16_t x, uint16_t y, uint8_t mipLevel, uint16_t frame) const
		{
			return GetPixel(x, y, 0, mipLevel, frame, 0);
		}

		/// <summary>
		/// Gets a pixel from the image at the specified coordinate and MIP level of a standard 2D texture
		/// </summary>
		/// <param name="x">Coordinate of the pixel on the x axis</param>
		/// <param name="y">Coordinate of the pixel on the y axis</param>
		/// <param name="mipLevel">MIP level to read (automatically transforms above coordinates)</param>
		/// <returns>Pixel struct with the pixel data</returns>
		inline Pixel GetPixel(uint16_t x, uint16_t y, uint8_t mipLevel) const
		{
			return GetPixel(x, y, mipLevel, 0);
		}

		/// <summary>
		/// Samples the texture at a given uv and performs filtering
		/// </summary>
		/// <param name="u">U coordinate</param>
		/// <param name="v">V coordinate</param>
		/// <param name="z">Coordinate of the pixel on the z axis (volumetric textures only)</param>
		/// <param name="mipLevel">MIP level to read</param>
		/// <param name="frame">Frame of the image (animated textures only)</param>
		/// <param name="face">Face of the image (envmaps only)</param>
		/// <returns>Pixel struct with the pixel data</returns>
		virtual Pixel Sample(float u, float v, uint16_t z, float mipLevel, uint16_t frame, uint8_t face) const = 0;

		/// <summary>
		/// Samples the texture at a given uv and performs filtering
		/// </summary>
		/// <param name="u">U coordinate</param>
		/// <param name="v">V coordinate</param>
		/// <param name="mipLevel">MIP level to read</param>
		/// <param name="face">Face of the image (envmaps only)</param>
		/// <returns>Pixel struct with the pixel data</returns>
		inline Pixel Sample(float u, float v, float mipLevel, uint16_t frame) const
		{
			return Sample(u, v, 0, mipLevel, frame, 0);
		}

		/// <summary>
		/// Samples the texture at a given uv and performs filtering
		/// </summary>
		/// <param name="u">U coordinate</param>
		/// <param name="v">V coordinate</param>
		/// <param name="mipLevel">MIP level to read</param>
		/// <returns>Pixel struct with the pixel data</returns>
		inline Pixel Sample(float u, float v, float mipLevel) const
		{
			return Sample(u, v, mipLevel, 0);
		}
	};
}

#pragma once

#include "vistrace/IVTFTexture.h"

#include <string>

namespace ResourceCache
{
	const VisTrace::IVTFTexture* GetTexture(const std::string& path, const std::string& fallback = "");

	void Clear();
}

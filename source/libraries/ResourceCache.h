#pragma once

#include "vistrace/IVTFTexture.h"
#include "Model.h"

#include <string>

namespace ResourceCache
{
	const VisTrace::IVTFTexture* GetTexture(const std::string& path, const std::string& fallback = "");
	const Model* GetModel(const std::string& path, const std::string& fallback = "");

	void Clear();
}

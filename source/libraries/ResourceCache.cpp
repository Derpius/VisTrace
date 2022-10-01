#include "ResourceCache.h"

#include "VTFTexture.h"

#include <unordered_map>
#include <new>

using namespace VisTrace;

static auto textureCache = std::unordered_map<std::string, const IVTFTexture*>();

const IVTFTexture* ResourceCache::GetTexture(const std::string& path, const std::string& fallback)
{
	if (textureCache.find(path) != textureCache.end()) return textureCache[path];

	if (!path.empty()) {
		const IVTFTexture* pTexture = new (std::nothrow) VTFTextureWrapper(path);
		if (pTexture != nullptr && pTexture->IsValid()) {
			textureCache.emplace(path, pTexture);
			return pTexture;
		}
		if (pTexture != nullptr) delete pTexture;
	}

	return (fallback.empty() || textureCache.find(fallback) == textureCache.end()) ? nullptr : textureCache[fallback];
}

void ResourceCache::Clear()
{
	for (const auto& [k, pTex] : textureCache) {
		delete pTex;
	}
	textureCache.clear();
}

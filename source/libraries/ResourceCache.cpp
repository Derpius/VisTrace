#include "ResourceCache.h"

#include "VTFTexture.h"

#include <unordered_map>
#include <new>

using namespace VisTrace;

static auto textureCache = std::unordered_map<std::string, const IVTFTexture*>();
static auto modelCache = std::unordered_map<std::string, const Model*>();

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

const Model* ResourceCache::GetModel(const std::string& path, const std::string& fallback)
{
	if (modelCache.find(path) != modelCache.end()) return modelCache[path];

	if (!path.empty()) {
		const Model* pModel = new (std::nothrow) Model(path.substr(0, path.length() - 4));
		if (pModel != nullptr && pModel->IsValid()) {
			modelCache.emplace(path, pModel);
			return pModel;
		}
		if (pModel != nullptr) delete pModel;
	}

	return (fallback.empty() || modelCache.find(fallback) == modelCache.end()) ? nullptr : modelCache[fallback];
}

void ResourceCache::Clear()
{
	for (const auto& [k, pTex] : textureCache) {
		delete pTex;
	}
	textureCache.clear();
}

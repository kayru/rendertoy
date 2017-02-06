#pragma once
#include "Common.h"
#include "Math.h"

#include <string>
#include <unordered_map>


struct TextureDesc {
	TextureDesc()
		: wrapS(true)
		, wrapT(true)
		, useRelativeScale(true)
		, relativeScale(1, 1)
		, resolution(1280, 720)
	{}

	enum class Source {
		Load,
		Create,
		Input
	};

	std::string path;
	Source source = Source::Create;
	std::string scaleRelativeTo;
	vec2 relativeScale;
	ivec2 resolution;
	bool wrapS : 1;
	bool wrapT : 1;
	bool useRelativeScale : 1;
};

struct TextureKey {
	u32 width;
	u32 height;
	unsigned int format;	// GLenum

	bool operator==(const TextureKey& other) const {
		return width == other.width && height == other.height && format == other.format;
	}
};

struct CreatedTexture {
	unsigned int texId = 0;			// GLuint
	unsigned int samplerId = 0;		// GLuint
	TextureKey key;

	~CreatedTexture();
};



extern std::unordered_map<std::string, shared_ptr<CreatedTexture>> g_loadedTextures;

shared_ptr<CreatedTexture> loadTexture(const TextureDesc& desc);
shared_ptr<CreatedTexture> createTexture(const TextureDesc& desc, const TextureKey& key);

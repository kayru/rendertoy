#include "Texture.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <tinyexr.h>

std::unordered_map<std::string, shared_ptr<CreatedTexture>> g_loadedTextures;

CreatedTexture::~CreatedTexture()
{
	if (texId != 0) glDeleteTextures(1, &texId);
	if (samplerId != 0) glDeleteSamplers(1, &samplerId);
}

shared_ptr<CreatedTexture> loadTexture(const TextureDesc& desc) {
	{
		auto found = g_loadedTextures.find(desc.path);
		if (found != g_loadedTextures.end()) {
			return found->second;
		}
	}

	int ret;
	const char* err;

	// 1. Read EXR version.
	EXRVersion exr_version;

	ret = ParseEXRVersionFromFile(&exr_version, desc.path.c_str());
	if (ret != 0) {
		fprintf(stderr, "Invalid EXR file: %s\n", desc.path.c_str());
		return nullptr;
	}

	if (exr_version.multipart) {
		// must be multipart flag is false.
		printf("Multipart EXR not supported");
		return nullptr;
	}

	// 2. Read EXR header
	EXRHeader exr_header;
	InitEXRHeader(&exr_header);

	ret = ParseEXRHeaderFromFile(&exr_header, &exr_version, desc.path.c_str(), &err);
	if (ret != 0) {
		fprintf(stderr, "Parse EXR err: %s\n", err);
		return nullptr;
	}

	EXRImage exr_image;
	InitEXRImage(&exr_image);

	for (int i = 0; i < exr_header.num_channels; ++i) {
		exr_header.requested_pixel_types[i] = TINYEXR_PIXELTYPE_HALF;
	}

	ret = LoadEXRImageFromFile(&exr_image, &exr_header, desc.path.c_str(), &err);
	if (ret != 0) {
		fprintf(stderr, "Load EXR err: %s\n", err);
		return nullptr;
	}

	short* out_rgba = nullptr;

	{
		// RGBA
		int idxR = -1;
		int idxG = -1;
		int idxB = -1;
		int idxA = -1;
		for (int c = 0; c < exr_header.num_channels; c++) {
			if (strcmp(exr_header.channels[c].name, "R") == 0) {
				idxR = c;
			}
			else if (strcmp(exr_header.channels[c].name, "G") == 0) {
				idxG = c;
			}
			else if (strcmp(exr_header.channels[c].name, "B") == 0) {
				idxB = c;
			}
			else if (strcmp(exr_header.channels[c].name, "A") == 0) {
				idxA = c;
			}
		}

		size_t imgSizeBytes = 4 * sizeof(short) * static_cast<size_t>(exr_image.width) * static_cast<size_t>(exr_image.height);

		out_rgba = reinterpret_cast<short *>(malloc(imgSizeBytes));
		memset(out_rgba, 0, imgSizeBytes);

		auto loadChannel = [&](int chIdx, int compIdx) {
			for (int y = 0; y < exr_image.height; ++y) {
				for (int x = 0; x < exr_image.width; ++x) {
					out_rgba[4 * (y * exr_image.width + x) + compIdx] =
						reinterpret_cast<short**>(exr_image.images)[chIdx][((exr_image.height - y - 1) * exr_image.width + x)];
				}
			}
		};

		if (idxR != -1) loadChannel(idxR, 0);
		if (idxG != -1) loadChannel(idxG, 1);
		if (idxB != -1) loadChannel(idxB, 2);

		if (idxA != -1) loadChannel(idxA, 3);
		else {
			const short one = 15 << 10;
			for (int i = 0; i < exr_image.width * exr_image.height; i++) {
				out_rgba[4 * i + 3] = one;
			}
		}
	}

	shared_ptr<CreatedTexture> res = createTexture(desc, TextureKey{ u32(exr_image.width), u32(exr_image.height), GL_RGBA16F });

	glBindTexture(GL_TEXTURE_2D, res->texId);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, exr_image.width, exr_image.height, GL_RGBA, GL_HALF_FLOAT, out_rgba);

	FreeEXRHeader(&exr_header);
	FreeEXRImage(&exr_image);

	g_loadedTextures[desc.path] = res;
	return res;
}


shared_ptr<CreatedTexture> createTexture(const TextureDesc& desc, const TextureKey& key)
{
	GLuint tex1;
	glGenTextures(1, &tex1);
	glBindTexture(GL_TEXTURE_2D, tex1);
	glTexStorage2D(GL_TEXTURE_2D, 1u, GL_RGBA16F, key.width, key.height);

	GLuint samplerId;
	glGenSamplers(1, &samplerId);
	glSamplerParameteri(samplerId, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glSamplerParameteri(samplerId, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	auto tex = std::make_shared<CreatedTexture>();
	tex->key = key;
	tex->texId = tex1;
	tex->samplerId = samplerId;
	return tex;
}

// ImGui - standalone example application for Glfw + OpenGL 3, using programmable pipeline
// If you are new to ImGui, see examples/README.txt and documentation at the top of imgui.cpp.

#include "Common.h"
#include "FileWatcher.h"
#include "Math.h"
#include "NodeGraph.h"

#include <imgui.h>
#include "imgui_impl_glfw_gl3.h"
#include <stdio.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <rapidjson/rapidjson.h>
#include <tinyexr.h>
#include <queue>
#include <string>
#include <memory>
#include <unordered_map>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <shellapi.h>

struct Pass;
Pass* g_editedPass = nullptr;

using std::vector;
using std::shared_ptr;
using std::make_shared;
namespace fs = ::std::experimental::filesystem;

bool ends_with(std::string const &fullString, std::string const &ending) {
	if (fullString.length() >= ending.length()) {
		return (0 == fullString.compare(fullString.length() - ending.length(), ending.length(), ending));
	}
	else {
		return false;
	}
}

// trim from start
static inline std::string &ltrim(std::string &s) {
	s.erase(s.begin(), std::find_if(s.begin(), s.end(),
		std::not1(std::ptr_fun<int, int>(std::isspace))));
	return s;
}

// trim from end
static inline std::string &rtrim(std::string &s) {
	s.erase(std::find_if(s.rbegin(), s.rend(),
		std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
	return s;
}

// trim from both ends
static inline std::string &trim(std::string &s) {
	return ltrim(rtrim(s));
}

// return the filenames of all files that have the specified extension
// in the specified directory and all subdirectories
void getFilesMatchingExtension(const fs::path& root, const std::string& ext, vector<fs::path>& ret)
{
	if (!fs::exists(root) || !fs::is_directory(root)) return;

	fs::recursive_directory_iterator it(root);
	fs::recursive_directory_iterator endit;

	while (it != endit)
	{
		if (fs::is_regular_file(*it) && it->path().extension() == ext) ret.push_back(it->path().filename());
		++it;
	}
}

static std::string getInfoLog(
	GLuint object,
	PFNGLGETSHADERIVPROC glGet__iv,
	PFNGLGETSHADERINFOLOGPROC glGet__InfoLog
)
{
	GLint log_length;
	char *log;

	glGet__iv(object, GL_INFO_LOG_LENGTH, &log_length);
	log = (char*)malloc(log_length);
	glGet__InfoLog(object, log_length, NULL, log);
	std::string result = log;
	free(log);

	return result;
}

std::vector<char> loadTextFileZ(const char* path)
{
	std::vector<char> programSource;
	const char* programSourceFile = path;
	FILE *f = fopen(programSourceFile, "rb");
	fseek(f, 0, SEEK_END);
	const int fsize = ftell(f);
	fseek(f, 0, SEEK_SET);
	programSource.resize(fsize + 1);
	fread(programSource.data(), 1, fsize, f);
	fclose(f);
	programSource.back() = '\0';
	return programSource;
}

std::vector<char> loadShaderSource(const std::string& path, const char* preprocessorOptions)
{
	/*std::string preprocessedFile = path + ".preprocessed";

	//if (getFileModifiedTime(preprocessedFile) < getFileModifiedTime(path))
	{
		std::string cmd = "cpp -P " + path + " -o " + preprocessedFile + " " + preprocessorOptions;
		system(cmd.c_str());
	}

	std::vector<char> result = loadTextFileZ(preprocessedFile.c_str());*/

	std::vector<char> result = loadTextFileZ(path.c_str());
	std::string prefix = "#version 440\n#line 0\n";

	result.insert(result.begin(), prefix.begin(), prefix.end());
	return result;
}

struct CreatedTexture {
	GLuint texId = -1;
	GLuint samplerId = -1;
	int width = 0;
	int height = 0;

	~CreatedTexture() {
		if (texId != -1) glDeleteTextures(1, &texId);
		if (samplerId != -1) glDeleteSamplers(1, &samplerId);
	}
};

struct TextureAsset {
	enum class Source {
		Load,
		Create,
		Input
	};

	std::string path;
	Source source = Source::Create;
	bool wrapS = true;
	bool wrapT = true;
	bool transientCreated = false;

	shared_ptr<CreatedTexture> texture;
};

void loadTexture(TextureAsset& asset) {
	asset.texture = nullptr;
	int ret;
	const char* err;

	// 1. Read EXR version.
	EXRVersion exr_version;

	ret = ParseEXRVersionFromFile(&exr_version, asset.path.c_str());
	if (ret != 0) {
		fprintf(stderr, "Invalid EXR file: %s\n", asset.path.c_str());
		return;
	}

	if (exr_version.multipart) {
		// must be multipart flag is false.
		printf("Multipart EXR not supported");
		return;
	}

	// 2. Read EXR header
	EXRHeader exr_header;
	InitEXRHeader(&exr_header);

	ret = ParseEXRHeaderFromFile(&exr_header, &exr_version, asset.path.c_str(), &err);
	if (ret != 0) {
		fprintf(stderr, "Parse EXR err: %s\n", err);
		return;
	}

	EXRImage exr_image;
	InitEXRImage(&exr_image);
	
	for (int i = 0; i < exr_header.num_channels; ++i) {
		exr_header.requested_pixel_types[i] = TINYEXR_PIXELTYPE_HALF;
	}

	ret = LoadEXRImageFromFile(&exr_image, &exr_header, asset.path.c_str(), &err);
	if (ret != 0) {
		fprintf(stderr, "Load EXR err: %s\n", err);
		return;
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

	GLuint tex1;
	glGenTextures(1, &tex1);
	glBindTexture(GL_TEXTURE_2D, tex1);
	glTexStorage2D(GL_TEXTURE_2D, 1u, GL_RGBA16F, exr_image.width, exr_image.height);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, exr_image.width, exr_image.height, GL_RGBA, GL_HALF_FLOAT, out_rgba);

	GLuint samplerId;
	glGenSamplers(1, &samplerId);
	glSamplerParameteri(samplerId, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glSamplerParameteri(samplerId, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	asset.texture = make_shared<CreatedTexture>();
	asset.texture->width = exr_image.width;
	asset.texture->height = exr_image.height;
	asset.texture->texId = tex1;
	asset.texture->samplerId = samplerId;

	FreeEXRHeader(&exr_header);
	FreeEXRImage(&exr_image);
}

struct ParamAnnotation
{
	std::unordered_map<std::string, std::string> items;

	const char* get(const std::string& value, const char* def) const {
		auto it = items.find(value);
		if (it != items.end()) {
			return it->second.c_str();
		} else {
			return def;
		}
	}

	float get(const std::string& value, float def) const {
		auto it = items.find(value);
		if (it != items.end()) {
			return atof(it->second.c_str());
		}
		else {
			return def;
		}
	}

	int get(const std::string& value, int def) const {
		auto it = items.find(value);
		if (it != items.end()) {
			return atoi(it->second.c_str());
		}
		else {
			return def;
		}
	}

	bool has(const std::string& value) const {
		return items.find(value) != items.end();
	}
};

struct ShaderParam {
	ShaderParam() {}

	void assignValue(const ShaderParam& other) {
		float4Value = other.float4Value;
		textureValue = other.textureValue;
	}

	enum class Type {
		Float,
		Float2,
		Float3,
		Float4,
		Int,
		Int2,
		Int3,
		Int4,
		Sampler2d,
		Image2d,
		Unknown,
	};

	std::string name;
	GLint location;
	Type type;
	ParamAnnotation annotation;

	union {
		float floatValue;
		vec2 float2Value;
		vec3 float3Value;
		vec4 float4Value;

		int intValue;
		ivec2 int2Value;
		ivec3 int3Value;
		ivec4 int4Value;
	};

	TextureAsset textureValue;
};

ShaderParam::Type parseShaderType(GLenum type, GLint size)
{
	static std::unordered_map<GLenum, ShaderParam::Type> tmap = {
		{ GL_FLOAT, ShaderParam::Type::Float },
		{ GL_FLOAT_VEC2, ShaderParam::Type::Float2 },
		{ GL_FLOAT_VEC3, ShaderParam::Type::Float3 },
		{ GL_FLOAT_VEC4, ShaderParam::Type::Float4 },
		{ GL_INT, ShaderParam::Type::Int },
		{ GL_INT_VEC2, ShaderParam::Type::Int2 },
		{ GL_INT_VEC3, ShaderParam::Type::Int3 },
		{ GL_INT_VEC4, ShaderParam::Type::Int4 },
		{ GL_SAMPLER_2D, ShaderParam::Type::Sampler2d },
		{ GL_IMAGE_2D, ShaderParam::Type::Image2d },
	};

	auto it = tmap.find(type);
	if (it != tmap.end()) {
		return it->second;
	}

	return ShaderParam::Type::Unknown;
}


bool parseParenthesizedExpression(const char*& c, const char* aend) {
	assert(*c == '(');
	++c;
	int level = 1;
	while (c < aend) {
		if ('(' == *c) ++level;
		else if (')' == *c) {
			--level;
			if (0 == level) return true;
		}
		++c;
	}

	return false;
}

bool parseAnnotation(const char* abegin, const char* aend, ParamAnnotation *const annot)
{
	const char* c = abegin;
	auto skipWhite = [&]() {
		while (c < aend && isspace(*c)) ++c;
		return c < aend;
	};

	while (c < aend) {
		skipWhite();

		if (isalnum(*c)) {
			const char* tbegin = c;
			while (isalnum(*c)) {
				++c;
			}
			const char* tend = c;

			skipWhite();

			std::string annotValue;
			if (*c == '(')
			{
				const char* const exprBegin = c;
				if (!parseParenthesizedExpression(c, aend)) {
					return false;
				}

				const char* const exprEnd = c;
				assert('(' == *exprBegin);
				assert(')' == *exprEnd);
				annotValue = std::string(exprBegin + 1, exprEnd);
				++c;
			}

			annot->items[std::string(tbegin, tend)] = annotValue;
		}
	}

	return true;
}

struct ComputeShader
{
	GLuint newCS = -1;
	GLuint newProgram = -1;
	std::vector<ShaderParam> params;
	std::vector<ShaderParam> previousParams;
	std::string errorLog;

	void reflectParams(const std::unordered_map<std::string, ParamAnnotation>& annotations)
	{
		params.clear();
		GLint activeUniformCount = 0;
		glGetProgramiv(newProgram, GL_ACTIVE_UNIFORMS, &activeUniformCount);
		printf("active uniform count: %d\n", activeUniformCount);

		char name[1024];

		for (GLint loc = 0; loc < activeUniformCount; ++loc) {
			GLsizei nameLength = 0;
			GLenum typeGl;
			GLint size;
			glGetActiveUniform(newProgram, loc, sizeof(name), &nameLength, &size, &typeGl, name);

			ShaderParam param;
			param.location = loc;
			param.name = name;
			param.type = parseShaderType(typeGl, size);
			
			auto it = annotations.find(name);
			if (it != annotations.end()) {
				param.annotation = it->second;
			}

			if (ShaderParam::Type::Float == param.type) {
				param.floatValue = param.annotation.get("default", 0.0f);
				params.push_back(param);
			} else if (ShaderParam::Type::Float2 == param.type) {
				param.float2Value = vec2(param.annotation.get("default", 0.0f));
				params.push_back(param);
			} else if (ShaderParam::Type::Float3 == param.type) {
				param.float3Value = vec3(param.annotation.get("default", param.annotation.has("color") ? 1.0f : 0.0f));
				params.push_back(param);
			} else if (ShaderParam::Type::Float4 == param.type) {
				param.float4Value = vec4(param.annotation.get("default", param.annotation.has("color") ? 1.0f : 0.0f));
				params.push_back(param);
			} else if (ShaderParam::Type::Int == param.type) {
				param.intValue = param.annotation.get("default", 0);
				params.push_back(param);
			} else if (ShaderParam::Type::Int2 == param.type) {
				param.int2Value = ivec2(param.annotation.get("default", 0));
				params.push_back(param);
			} else if (ShaderParam::Type::Int3 == param.type) {
				param.int3Value = ivec3(param.annotation.get("default", 0));
				params.push_back(param);
			} else if (ShaderParam::Type::Int4 == param.type) {
				param.int4Value = ivec4(param.annotation.get("default", 0));
				params.push_back(param);
			} else if (ShaderParam::Type::Sampler2d == param.type) {
				if (param.annotation.has("input")) {
					param.textureValue.source = TextureAsset::Source::Input;
				} else if (param.annotation.has("default")) {
					param.textureValue.path = param.annotation.get("default", "");
					param.textureValue.source = TextureAsset::Source::Load;
					loadTexture(param.textureValue);
				}
				params.push_back(param);
			} else if (ShaderParam::Type::Image2d == param.type) {
				if (param.annotation.has("input")) {
					param.textureValue.source = TextureAsset::Source::Input;
				} else if (param.annotation.has("default")) {
					param.textureValue.path = param.annotation.get("default", "");
					param.textureValue.source = TextureAsset::Source::Load;
					loadTexture(param.textureValue);
				}
				params.push_back(param);
			}
		}
	}

	void copyParamValues(const ComputeShader& other)
	{
		std::unordered_map<std::string, const ShaderParam*> otherParams;
		for (const ShaderParam& p : other.params) {
			otherParams[p.name] = &p;
		}
		for (const ShaderParam& p : other.previousParams) {
			auto op = otherParams.find(p.name);
			if (op == otherParams.end()) {
				otherParams[p.name] = &p;
			}			
		}

		for (ShaderParam& p : this->params) {
			auto op = otherParams.find(p.name);
			if (op != otherParams.end() && op->second->type == p.type) {
				p.assignValue(*op->second);
				otherParams.erase(op);
			}
		}

		for (auto& p : otherParams) {
			previousParams.push_back(*p.second);
		}
	}

	std::unordered_map<std::string, ParamAnnotation> parseAnnotations(const std::vector<char>& source) {
		std::unordered_map<std::string, ParamAnnotation> result;

		auto processLine = [&](const char* lbegin, const char* lend) {
			while (lend > lbegin && lend[-1] == '\r') --lend;

			const char* tagBegin = "//@";
			const char* tagEnd = tagBegin + strlen(tagBegin);
			const char* annotBegin = std::search(lbegin, lend, tagBegin, tagEnd);
			if (annotBegin == lend) return;

			const char* identEnd = annotBegin;

			// Skip the tag from the annotation
			annotBegin += tagEnd - tagBegin;

			// Find the identifier
			while (identEnd > lbegin && *identEnd != ';') --identEnd;
			if (lbegin == identEnd) return;

			while (identEnd > lbegin && isspace(*identEnd)) --identEnd;
			if (lbegin == identEnd) return;

			const char* identBegin = identEnd - 1;
			while (identBegin > lbegin && (isalnum(*identBegin) || '_' == *identBegin)) --identBegin;
			++identBegin;

			if (identEnd - identBegin <= 0) {
				return;
			}

			std::string paramName = std::string(identBegin, identEnd);
			//auto param = std::find_if(params.begin(), params.end(), [&paramName](ShaderParam& it) { return it.name == paramName; });
			//if (param != params.end())
			{
				ParamAnnotation annot;
				if (parseAnnotation(annotBegin, lend, &annot)) {
					result[paramName] = annot;
				}
			}

			//printf("Ident: '%.*s' annotation: '%.*s'\n", int(identEnd - identBegin), identBegin, int(lend - annotBegin), annotBegin);
		};

		const char* lbegin = source.data();
		const char *const fend = source.data() + source.size();
		for (const char* lend = source.data(); lend != fend; ++lend) {
			if ('\n' == *lend) {
				processLine(lbegin, lend);
				lbegin = lend + 1;
			}
		}

		if (fend - lbegin > 1) {
			processLine(lbegin, fend);
		}

		return result;
	}

	ComputeShader() {}
	ComputeShader(const std::vector<char>& csSource, const std::string sourceFile)
	{
		const GLchar* sources[1];
		GLint         success;

		newCS = glCreateShader(GL_COMPUTE_SHADER);
		newProgram = glCreateProgram();
		glAttachShader(newProgram, newCS);

		sources[0] = csSource.data();
		glShaderSource(newCS, 1, sources, NULL);
		glCompileShader(newCS);
		{
			GLint shader_ok;
			glGetShaderiv(newCS, GL_COMPILE_STATUS, &shader_ok);

			if (!shader_ok) {
				printf("Failed to compile shader:\n");
				this->errorLog = getInfoLog(newCS, glGetShaderiv, glGetShaderInfoLog);
				glDeleteShader(newCS);
				glDeleteProgram(newProgram);
				newCS = -1;
				newProgram = -1;
				//getchar();
				//exit(1);
			}
		}
		glLinkProgram(newProgram);
		/*{
			GLint shader_ok;
			glGetProgramiv(newCS, GL_LINK_STATUS, &shader_ok);

			if (!shader_ok) {
				printf("Failed to link shader:\n");
				showInfoLog(newProgram, glGetProgramiv, glGetProgramInfoLog);
				glDeleteShader(newCS);
				glDeleteProgram(newProgram);
				newCS = -1;
				newProgram = -1;
				//getchar();
				//exit(1);
			}
		}*/

		if (this->errorLog.length() > 0) {
			std::ofstream(sourceFile + ".errors").write(this->errorLog.data(), this->errorLog.size());
		} else {
			remove(fs::path(sourceFile + ".errors"));
		}

		if (newProgram != -1) {
			auto annotations = parseAnnotations(csSource);
			reflectParams(annotations);
		}
	}
};

struct Pass
{
	Pass(const std::string& shaderPath)
		: m_shaderPath(shaderPath)
	{
		m_computeShader = ComputeShader(loadShaderSource(shaderPath, ""), shaderPath);
		FileWatcher::watchFile(shaderPath.c_str(), [shaderPath, this]()
		{
			ComputeShader shader(loadShaderSource(shaderPath, ""), shaderPath);
			if (shader.newProgram != -1) {
				shader.copyParamValues(m_computeShader);
				m_computeShader = shader;
			} else {
				m_computeShader.errorLog = shader.errorLog;
			}
		});
	}

	std::string m_shaderPath;
	ComputeShader m_computeShader;
	NodeInfo m_nodeInfo;
};

struct Package
{
	vector<shared_ptr<Pass>> m_passes;

	void handleFileDrop(const std::string& path)
	{
		if (ends_with(path, ".glsl")) {
			auto pass = make_shared<Pass>(path);
			m_passes.emplace_back(pass);

			//ImGui::Node* n1 = nge.addNode(0, ImVec2(200, 50));
		}
	}
};

struct Project
{
	vector<shared_ptr<Package>> m_packages;

	void handleFileDrop(const std::string& path)
	{
		m_packages.back()->handleFileDrop(path);
	}
};

Project g_project;

#define NOMINMAX
//#include <windows.h>
#include <commdlg.h>
#undef max
#undef min
#include <algorithm>

bool doTextureLoadUi(ShaderParam& param)
{
	bool res = false;

	if (ImGui::Button("Browse...")) {
		OPENFILENAME ofn = {};
		char filename[1024] = { '\0' };
		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFilter = "Image Files\0*.exr\0";
		ofn.lpstrFile = filename;
		ofn.nMaxFile = sizeof(filename);
		ofn.lpstrTitle = "Select an image";
		ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
		if (GetOpenFileNameA(&ofn))
		{
			param.textureValue.path = filename;
			res = true;
		}
	}

	ImGui::SameLine();
	ImGui::Text(param.textureValue.path.c_str());

	return res;
}

void doPassUi(Pass& pass)
{
	int maxLabelWidth = 0;
	for (auto& param : pass.m_computeShader.params) {
		maxLabelWidth = std::max(maxLabelWidth, (int)ImGui::CalcTextSize(param.name.c_str()).x);
	}
	maxLabelWidth += 10;

	for (auto& param : pass.m_computeShader.params) {
		ImGui::PushID(param.name.c_str());
		ImGui::Columns(2);
		{
			const int textWidth = (int)ImGui::CalcTextSize(param.name.c_str()).x;
			ImGui::SetCursorPosX(maxLabelWidth - textWidth);
			ImGui::Text(param.name.c_str());
		}

		ImGui::SetColumnOffset(1, maxLabelWidth + 10);
		ImGui::NextColumn();

		if (param.type == ShaderParam::Type::Float) {
			ImGui::SliderFloat("", &param.floatValue, param.annotation.get("min", 0.0f), param.annotation.get("max", 1.0f));
		} else if (param.type == ShaderParam::Type::Float2) {
			ImGui::SliderFloat2("", &param.float2Value.x, param.annotation.get("min", 0.0f), param.annotation.get("max", 1.0f));
		} else if (param.type == ShaderParam::Type::Float3) {
			if (param.annotation.has("color")) {
				ImGui::ColorEdit3("", &param.float3Value.x);
			} else {
				ImGui::SliderFloat3("", &param.float3Value.x, param.annotation.get("min", 0.0f), param.annotation.get("max", 1.0f));
			}
		} else if (param.type == ShaderParam::Type::Float4) {
			if (param.annotation.has("color")) {
				ImGui::ColorEdit4("", &param.float4Value.x);
			} else {
				ImGui::SliderFloat4("", &param.float4Value.x, param.annotation.get("min", 0.0f), param.annotation.get("max", 1.0f));
			}
		} else if (param.type == ShaderParam::Type::Int) {
			ImGui::SliderInt("", &param.intValue, param.annotation.get("min", 0), param.annotation.get("max", 16));
		} else if (param.type == ShaderParam::Type::Int2) {
			ImGui::SliderInt2("", &param.int2Value.x, param.annotation.get("min", 0), param.annotation.get("max", 16));
		} else if (param.type == ShaderParam::Type::Int3) {
			ImGui::SliderInt3("", &param.int3Value.x, param.annotation.get("min", 0), param.annotation.get("max", 16));
		} else if (param.type == ShaderParam::Type::Int4) {
			ImGui::SliderInt4("", &param.int4Value.x, param.annotation.get("min", 0), param.annotation.get("max", 16));
		} else if (param.type == ShaderParam::Type::Sampler2d) {
			{
				ImGui::PushID("wrapS");
				int wrapS = param.textureValue.wrapS ? 0 : 1;
				const char* const wrapSValues[] = {
					"Wrap S",
					"Clamp S",
				};
				ImGui::PushItemWidth(100);
				ImGui::Combo("", &wrapS, wrapSValues, sizeof(wrapSValues) / sizeof(*wrapSValues));
				param.textureValue.wrapS = !wrapS;
				ImGui::PopID();
			}

			ImGui::SameLine();

			{
				ImGui::PushID("wrapT");
				int wrapT = param.textureValue.wrapT ? 0 : 1;
				const char* const wrapTValues[] = {
					"Wrap T",
					"Clamp T",
				};
				ImGui::PushItemWidth(100);
				ImGui::Combo("", &wrapT, wrapTValues, sizeof(wrapTValues) / sizeof(*wrapTValues));
				param.textureValue.wrapT = !wrapT;
				ImGui::PopID();
			}

			ImGui::SameLine();
			doTextureLoadUi(param);
		} else if (param.type == ShaderParam::Type::Image2d) {
			ImGui::BeginGroup();
			int sourceIdx = int(param.textureValue.source);
			const char* const sources[] = {
				"Load",
				"Create",
				"Input",
			};
			ImGui::PushID("source");
			ImGui::PushItemWidth(100);
			bool selected = ImGui::Combo("", &sourceIdx, sources, sizeof(sources) / sizeof(*sources));
			ImGui::PopID();
			const auto prevSource = param.textureValue.source;
			param.textureValue.source = TextureAsset::Source(sourceIdx);

			if (TextureAsset::Source::Load == param.textureValue.source) {
				ImGui::SameLine();
				if (selected || doTextureLoadUi(param)) {
					loadTexture(param.textureValue);
				}
			} else if (TextureAsset::Source::Create == param.textureValue.source) {
				int formatIdx = 0;
				const char* const formats[] = {
					"rgba16f",
					"r11g11b10f",
				};

				ImGui::SameLine();
				ImGui::PushItemWidth(100);
				ImGui::Combo("", &formatIdx, formats, sizeof(formats)/sizeof(*formats));

				ImGui::SameLine();
				static bool relativeSize = true;
				ImGui::Checkbox("relative", &relativeSize);

				if (relativeSize) {
					ImGui::PushID("relativeSize");
					float size[] = { 1.0, 1.0 };
					ImGui::PushItemWidth(100);
					ImGui::SameLine();
					ImGui::InputFloat2("scale", size, 2);
					ImGui::SameLine();

					int target = 0;
					static std::vector<const char*> targets;
					targets = {
						"window",
						"foo",
						"bar",
					};

					int sizeNeeded = 0;
					for (const char* str : targets) {
						sizeNeeded = std::max(sizeNeeded, (int)ImGui::CalcTextSize(str).x);
					}
					sizeNeeded += 32;

					ImGui::PushItemWidth(sizeNeeded);
					ImGui::SameLine();
					ImGui::Combo("", &target, targets.data(), targets.size());
					ImGui::PopID();
				} else {
					int size[] = { 256, 256 };
					ImGui::PushItemWidth(100);
					ImGui::SameLine();
					ImGui::InputInt2("resolution", size);
				}
			} else {
				if (prevSource != param.textureValue.source) {
					param.textureValue.texture = nullptr;
					param.textureValue.transientCreated = false;
				}
			}

			ImGui::EndGroup();
		}

		ImGui::Columns(1);
		ImGui::PopID();
	}

	if (ImGui::Button("Edit shader")) {
		char shaderPath[1024];
		GetFullPathName(pass.m_shaderPath.c_str(), sizeof(shaderPath), shaderPath, nullptr);
		ShellExecuteA(0, nullptr, shaderPath, 0, 0, SW_SHOW);
	}

	if (!pass.m_computeShader.errorLog.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.2, 0.1, 1));
		ImGui::Text("Compile error:\n%s", pass.m_computeShader.errorLog.c_str());
		ImGui::PopStyleColor();
	}
	/*ImGui::Text("Hello, world!");
	ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
	ImGui::ColorEdit3("clear color", (float*)&clearColor);
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);*/
}

static void windowErrorCallback(int error, const char* description)
{
	fprintf(stderr, "Error %d: %s\n", error, description);
}

std::vector<std::string> editorFileDrops;

static void windowDropCallback(GLFWwindow* window, int count, const char** files)
{
	if (nullptr == g_editedPass) {
		while (count--) {
			editorFileDrops.push_back(*files++);
		}
	}
}

struct WindowEvent {
	enum class Type {
		Keyboard,
	} type;

	struct Keyboard {
		int key;
		int scancode;
		int action;
		int mods;
	};

	union {
		Keyboard keyboard;
	};
};

GLFWwindow* g_mainWindow = nullptr;
std::queue<WindowEvent> g_windowEvents;

extern void ImGui_ImplGlfwGL3_KeyCallback(GLFWwindow*, int, int, int, int);
static void windowKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	WindowEvent e = { WindowEvent::Type::Keyboard };
	e.keyboard = WindowEvent::Keyboard { key, scancode, action, mods };
	g_windowEvents.emplace(e);
	ImGui_ImplGlfwGL3_KeyCallback(window, key, scancode, action, mods);
}

void doMainMenu()
{
	if (ImGui::BeginMenu("File")) {
		if (ImGui::MenuItem("New", nullptr)) {

		}
		if (ImGui::MenuItem("Open", nullptr)) {

		}
		if (ImGui::MenuItem("Save", nullptr)) {

		}
		if (ImGui::MenuItem("Exit", nullptr)) {
			glfwSetWindowShouldClose(g_mainWindow, 1);
		}

		ImGui::EndMenu();
	}
}

void drawFullscreenQuad(GLuint tex)
{
	const GLchar *vertex_shader =
		"#version 330\n"
		"out vec2 Frag_UV;\n"
		"void main()\n"
		"{\n"
		"	Frag_UV = vec2(gl_VertexID & 1, gl_VertexID >> 1) * 2.0;\n"
		"	gl_Position = vec4(Frag_UV * 2.0 - 1.0, 0, 1);\n"
		"}\n";

	const GLchar* fragment_shader =
		"#version 330\n"
		"uniform sampler2D Texture;\n"
		"in vec2 Frag_UV;\n"
		"out vec4 Out_Color;\n"
		"void main()\n"
		"{\n"
		//"	Out_Color = vec4(Frag_UV, 0, 1);\n"
		"	Out_Color = texture(Texture, Frag_UV);\n"
		"}\n";

	static GLuint g_ShaderHandle = -1, g_VertHandle, g_FragHandle;

	if (-1 == g_ShaderHandle) {
		g_ShaderHandle = glCreateProgram();
		g_VertHandle = glCreateShader(GL_VERTEX_SHADER);
		g_FragHandle = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(g_VertHandle, 1, &vertex_shader, 0);
		glShaderSource(g_FragHandle, 1, &fragment_shader, 0);
		glCompileShader(g_VertHandle);
		glCompileShader(g_FragHandle);
		glAttachShader(g_ShaderHandle, g_VertHandle);
		glAttachShader(g_ShaderHandle, g_FragHandle);
		glLinkProgram(g_ShaderHandle);
	}

	glUseProgram(g_ShaderHandle);

	glActiveTexture(0);
	glBindTexture(GL_TEXTURE_2D, tex);

	const GLint loc = glGetUniformLocation(g_ShaderHandle, "Texture");
	const GLint img_unit = 0;
	glUniform1i(loc, img_unit);

	glDrawArrays(GL_TRIANGLES, 0, 3);
	glUseProgram(0);
}

void createPassImages(Pass& pass, int width, int height)
{
	for (auto& param : pass.m_computeShader.params) {
		if (ShaderParam::Type::Image2d == param.type) {
			TextureAsset& tex = param.textureValue;

			if (tex.source != TextureAsset::Source::Create && tex.transientCreated) {
				tex.texture = nullptr;
			} else if (tex.source == TextureAsset::Source::Create) {
				if (tex.texture && (tex.texture->width != width || tex.texture->height != height)) {
					tex.texture = nullptr;
				}

				if (!tex.texture) {
					GLuint tex1;
					glGenTextures(1, &tex1);
					glBindTexture(GL_TEXTURE_2D, tex1);
					glTexStorage2D(GL_TEXTURE_2D, 1u, GL_RGBA16F, width, height);

					tex.texture = std::make_shared<CreatedTexture>();
					tex.texture->width = width;
					tex.texture->height = height;
					tex.texture->samplerId = -1;
					tex.texture->texId = tex1;
				}
			}
		}
	}
}

void renderProject(int width, int height)
{
	GLuint outputTexId = -1;

	for (shared_ptr<Package>& package : g_project.m_packages) {
		for (shared_ptr<Pass>& pass : package->m_passes) {
			createPassImages(*pass, width, height);

			GLint img_unit = 0;
			GLint tex_unit = 0;

			/*
			// TODO
			static GLuint tex1 = -1;
			static int prevWidth = 0;
			static int prevHeight = 0;

			if (prevWidth != width || prevHeight != height && tex1 != -1) {
				glDeleteTextures(1, &tex1);
				tex1 = -1;
			}

			if (-1 == tex1) {
				glGenTextures(1, &tex1);
				glBindTexture(GL_TEXTURE_2D, tex1);
				glTexStorage2D(GL_TEXTURE_2D, 1u, GL_RGBA16F, width, height);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				prevWidth = width;
				prevHeight = height;
			}

			const GLint level = 0;
			const GLenum layered = GL_FALSE;
			glBindImageTexture(img_unit, tex1, level, layered, 0, GL_READ_WRITE, GL_RGBA16F);
			++img_unit;*/

			glUseProgram(pass->m_computeShader.newProgram);

			for (const auto& param : pass->m_computeShader.params) {
				if (param.type == ShaderParam::Type::Float) {
					glUniform1f(param.location, param.floatValue);
				} else if (param.type == ShaderParam::Type::Float2) {
					glUniform2f(param.location, param.float2Value.x, param.float2Value.y);
				} else if (param.type == ShaderParam::Type::Float3) {
					glUniform3f(param.location, param.float3Value.x, param.float3Value.y, param.float3Value.z);
				} else if (param.type == ShaderParam::Type::Float4) {
					glUniform4f(param.location, param.float4Value.x, param.float4Value.y, param.float4Value.z, param.float4Value.w);
				} else if (param.type == ShaderParam::Type::Int) {
					glUniform1i(param.location, param.intValue);
				} else if (param.type == ShaderParam::Type::Int2) {
					glUniform2i(param.location, param.int2Value.x, param.int2Value.y);
				} else if (param.type == ShaderParam::Type::Int3) {
					glUniform3i(param.location, param.int3Value.x, param.int3Value.y, param.int3Value.z);
				} else if (param.type == ShaderParam::Type::Int4) {
					glUniform4i(param.location, param.int4Value.x, param.int4Value.y, param.int4Value.z, param.int4Value.w);
				} else if (param.type == ShaderParam::Type::Image2d) {
					if (param.textureValue.texture) {
						const GLint level = 0;
						const GLenum layered = GL_FALSE;
						glBindImageTexture(img_unit, param.textureValue.texture->texId, level, layered, 0, GL_READ_ONLY, GL_RGBA16F);
						glUniform1i(param.location, img_unit);
						++img_unit;

						// HACK
						if (param.name == "outputTex") {
							outputTexId = param.textureValue.texture->texId;
						}
					}
				} else if (param.type == ShaderParam::Type::Sampler2d) {
					if (param.textureValue.texture) {
						const GLint level = 0;
						const GLenum layered = GL_FALSE;
						glActiveTexture(GL_TEXTURE0 + tex_unit);
						glBindTexture(GL_TEXTURE_2D, param.textureValue.texture->texId);
						glUniform1i(param.location, tex_unit);

						const GLuint samplerId = param.textureValue.texture->samplerId;
						glSamplerParameteri(samplerId, GL_TEXTURE_WRAP_S, param.textureValue.wrapS ? GL_REPEAT : GL_CLAMP_TO_EDGE);
						glSamplerParameteri(samplerId, GL_TEXTURE_WRAP_T, param.textureValue.wrapT ? GL_REPEAT : GL_CLAMP_TO_EDGE);
						glBindSampler(tex_unit, samplerId);
						++tex_unit;
					}
				}
			}

			GLint workGroupSize[3];
			glGetProgramiv(pass->m_computeShader.newProgram, GL_COMPUTE_WORK_GROUP_SIZE, workGroupSize);
			glDispatchCompute(
				(width + workGroupSize[0] - 1) / workGroupSize[0],
				(height + workGroupSize[1] - 1) / workGroupSize[1],
				1);
		}

		if (outputTexId != -1) {
			drawFullscreenQuad(outputTexId);
		}
	}
}

void APIENTRY openGLDebugCallback(
	GLenum source,
	GLenum type,
	GLuint id,
	GLenum severity,
	GLsizei length,
	const GLchar* message,
	const void* userParam
) {
	if (GL_DEBUG_SEVERITY_NOTIFICATION == severity) {
		printf("GL debug: %s", message);
	}
	else {
		puts(message);
		abort();
	}
}

struct ShaderNodeBackend : INodeGraphBackend
{
	shared_ptr<Package> shaderPackage = nullptr;
	const NodeInfo* triggeredNode = nullptr;

	static Pass* getPass(const NodeInfo* ni) {
		return reinterpret_cast<Pass*>(size_t(ni) - ptrdiff_t(&((Pass*)nullptr)->m_nodeInfo));
	}

	size_t getNodeCount() override
	{
		return shaderPackage->m_passes.size();
	}

	virtual NodeInfo* getNodeByIdx(size_t idx) override
	{
		Pass& pass = *shaderPackage->m_passes[idx];
		NodeInfo& ni = pass.m_nodeInfo;
		{
			std::string filename = fs::path(pass.m_shaderPath).filename().string();
			ni.name = filename.substr(0, filename.find_last_of("."));
		}

		ni.inputs.clear();
		ni.outputs.clear();

		for (ShaderParam& p : pass.m_computeShader.params) {
			if (p.type == ShaderParam::Type::Image2d) {
				if (TextureAsset::Source::Create == p.textureValue.source) {
					ni.outputs.push_back({(size_t)p.name.data(), p.name});
				} else if (TextureAsset::Source::Input == p.textureValue.source) {
					ni.inputs.push_back({ (size_t)p.name.data(), p.name });
				}
			}
		}

		return &ni;
	}

	void onContextMenu(const NodeInfo* node) override
	{
		if (node)
		{
			ImGui::Text("Node '%s'", node->name.data);
			ImGui::Separator();
			if (ImGui::MenuItem("Rename..", NULL, false, false)) {}
			if (ImGui::MenuItem("Delete", NULL, false, false)) {}
			if (ImGui::MenuItem("Copy", NULL, false, false)) {}
		}
		else
		{
			//if (ImGui::MenuItem("Add")) { nodes.push_back(Node(nodes.Size, "New node", scene_pos, 0.5f, ImColor(100,100,200), 2, 2)); }
			//if (ImGui::MenuItem("Paste", NULL, false, false)) {}
			std::vector<std::string> items;
			getGlobalContextMenuItems(&items);

			for (std::string& it : items) {
				if (ImGui::MenuItem(it.c_str(), NULL, false, true)) {
					onGlobalContextMenuSelected(it);
				}
			}
		}
	}

	void getGlobalContextMenuItems(std::vector<std::string> *const items)
	{
		std::vector<fs::path> files;
		getFilesMatchingExtension("data", ".glsl", files);

		for (const fs::path& f : files) {
			std::string filename = f.filename().string();
			items->push_back(filename.substr(0, filename.find_last_of(".")));
		}
	}

	void onGlobalContextMenuSelected(const std::string& shaderFile)
	{
		g_project.handleFileDrop("data/" + shaderFile + ".glsl");
	}

	// Called once to initialize connections
	//void getLinks(std::vector<LinkInfo> *const) override {}

	bool onConnected(const LinkInfo& l) override {
		Pass& src = *getPass(l.srcNode);
		Pass& dst = *getPass(l.dstNode);

		// HACK

		const char* srcName = src.m_nodeInfo.outputs[l.srcPort].name.data;
		const char* dstName = dst.m_nodeInfo.inputs[l.dstPort].name.data;
		auto p0 = std::find_if(src.m_computeShader.params.begin(), src.m_computeShader.params.end(), [srcName](const ShaderParam& p) { return p.name == srcName; });
		auto p1 = std::find_if(dst.m_computeShader.params.begin(), dst.m_computeShader.params.end(), [dstName](const ShaderParam& p) { return p.name == dstName; });
		if (p0 == src.m_computeShader.params.end()) return false;
		if (p1 == dst.m_computeShader.params.end()) return false;
		if (p0->type != p1->type || p0->type != ShaderParam::Type::Image2d) return false;
		p1->textureValue.texture = p0->textureValue.texture;

		return true;
	}

	void onDisconnected(const LinkInfo& l) override {}

	void onTriggered(const NodeInfo* node) override {
		triggeredNode = node;
	}
};


//int main(int, char**)
int CALLBACK WinMain(
	_In_ HINSTANCE hInstance,
	_In_ HINSTANCE hPrevInstance,
	_In_ LPSTR     lpCmdLine,
	_In_ int       nCmdShow
) {
	// Setup window
	glfwSetErrorCallback(&windowErrorCallback);
	FileWatcher::start();

	if (!glfwInit()) {
		return 1;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
	//glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
	//glfwWindowHint(GLFW_MAXIMIZED, 1);
	//glfwWindowHint(GLFW_DECORATED, 0);
	//glfwWindowHint(GLFW_FLOATING, 1);

	GLFWmonitor* monitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* vidMode = glfwGetVideoMode(monitor);
	g_mainWindow = glfwCreateWindow(vidMode->width / 2, vidMode->height, "RenderToy", NULL, NULL);
	GLFWwindow*& window = g_mainWindow;

	/*{
		int x1, y1, w, h;
		glfwGetWindowPos(window, &x1, &y1);
		glfwGetWindowSize(window, &w, &h);

		glfwRestoreWindow(window);

		glfwSetWindowSize(window, vidMode->width / 2, h);

		glfwGetWindowSize(window, &w, &h);
		//glfwSetWindowPos(window, x1, y1);

		// Stupid hack; assumes start menu is on the left/right
		glfwSetWindowPos(window, x1, vidMode->height - h);
	}*/

#if 0
	//glfwSetWindowAttrib(window, GLFW_DECORATED, 0);
	//{
		int x1, y1;
		glfwGetWindowSize(window, &x1, &y1);

		//glfwMaximizeWindow(window);
		glfwRestoreWindow(window);

		/*int w, h;
		glfwGetWindowSize(window, &w, &h);
		glfwSetWindowSize(window, vidMode->width / 2, h);*/

		int x2, y2;
		glfwGetWindowSize(window, &x2, &y2);
	//}

#endif
	{
		int x, y;
		glfwGetWindowPos(window, &x, &y);
		glfwRestoreWindow(window);
		glfwSetWindowPos(window, x, y);
	}

	glfwSetInputMode(window, GLFW_STICKY_KEYS, 1);
	glfwSetDropCallback(window, &windowDropCallback);
	glfwSetKeyCallback(window, &windowKeyCallback);

	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);
	gladLoadGL();

	glDebugMessageCallback(&openGLDebugCallback, nullptr);
	glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, 1);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

	// Setup ImGui binding
	ImGui_ImplGlfwGL3_Init(window, true);

	g_project.m_packages.push_back(make_shared<Package>());

	ImVec4 clearColor = ImColor(75, 75, 75);
	bool fullscreen = false;
	bool maximized = false;
	float f;

	// Main loop
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		ImGui_ImplGlfwGL3_NewFrame();

		bool toggleFullscreen = false;
		bool toggleMaximized = false;

		while (!g_windowEvents.empty()) {
			const WindowEvent& e = g_windowEvents.back();
			if (e.type == WindowEvent::Type::Keyboard) {
				if (e.keyboard.key == GLFW_KEY_F11 && e.keyboard.action == GLFW_PRESS) {
					toggleFullscreen = true;
				}

				if (e.keyboard.key == GLFW_KEY_F10 && e.keyboard.action == GLFW_PRESS) {
					toggleMaximized = true;
				}

				if (e.keyboard.key == GLFW_KEY_ESCAPE && e.keyboard.action == GLFW_PRESS) {
					if (g_editedPass) {
						g_editedPass = nullptr;
					}
				}
			}
			g_windowEvents.pop();
		}

		if (!fullscreen)
		{
			ImGui::BeginMainMenuBar();
			const int mainMenuHeight = ImGui::GetWindowHeight();
			doMainMenu();
			ImGui::EndMainMenuBar();

			int windowWidth, windowHeight;
			glfwGetWindowSize(window, &windowWidth, &windowHeight);
			ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight / 2 - mainMenuHeight), ImGuiSetCond_Always);
			ImGui::SetNextWindowPos(ImVec2(0, mainMenuHeight), ImGuiSetCond_Always);

			ImGuiWindowFlags windowFlags = 0;
			windowFlags |= ImGuiWindowFlags_NoTitleBar;
			windowFlags |= ImGuiWindowFlags_NoResize;
			windowFlags |= ImGuiWindowFlags_NoMove;
			windowFlags |= ImGuiWindowFlags_NoCollapse;

			ImGui::PushStyleColor(ImGuiCol_WindowBg, ImColor(40, 40, 40, 255));
			bool windowOpen = true;
			ImGui::Begin("Another Window", &windowOpen, windowFlags);

			if (g_editedPass) {
				bool windowOpen = true;
				ImGui::Begin("Another Window", &windowOpen, windowFlags);
				doPassUi(*g_editedPass);
				ImGui::End();
			} else if (g_project.m_packages.size() > 0) {
				static ShaderNodeBackend nodeBackend;
				nodeBackend.triggeredNode = nullptr;
				nodeBackend.shaderPackage = g_project.m_packages[0];
				nodeGraph(&nodeBackend);
				if (nodeBackend.triggeredNode != nullptr) {
					g_editedPass = ShaderNodeBackend::getPass(nodeBackend.triggeredNode);
				}
			}

			ImGui::End();
			ImGui::PopStyleColor();
		}

		// Rendering
		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
		glClear(GL_COLOR_BUFFER_BIT);

		const u32 renderHeight = fullscreen ? display_h : display_h / 2;
		glViewport(0, 0, display_w, renderHeight);
		glScissor(0, 0, display_w, renderHeight);
		glEnable(GL_FRAMEBUFFER_SRGB);
		renderProject(display_w, renderHeight);
		glDisable(GL_FRAMEBUFFER_SRGB);

		glViewport(0, 0, display_w, display_h);
		glScissor(0, 0, display_w, display_h);
		ImGui::Render();

		glfwSwapBuffers(window);
		FileWatcher::update();

		if (!fullscreen && toggleMaximized) {
			static int prevX, prevY, prevW, prevH;
			if (maximized) {
				glfwRestoreWindow(window);
				glfwSetWindowPos(window, prevX, prevY);
				glfwSetWindowSize(window, prevW, prevH);
			} else {
				glfwGetWindowPos(window, &prevX, &prevY);
				glfwGetWindowSize(window, &prevW, &prevH);
				glfwMaximizeWindow(window);
			}
			maximized = !maximized;
		}

		if (toggleFullscreen) {
			static int lastX, lastY, lastWidth, lastHeight;

			fullscreen = !fullscreen;
			if (fullscreen) {
				glfwGetWindowPos(window, &lastX, &lastY);
				glfwGetWindowSize(window, &lastWidth, &lastHeight);

				//glfwSetWindowPos(window, 0, 0);
				//glfwSetWindowAttrib(window, GLFW_FLOATING, 1);
				//glfwSetWindowAttrib(window, GLFW_DECORATED, 0);
				//glfwSetWindowAttrib(window, GLFW_MAXIMIZED, 1);

				const GLFWvidmode* mode = glfwGetVideoMode(monitor);
				glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
				glfwSwapInterval(1);
			} else {
				//glfwSetWindowAttrib(window, GLFW_FLOATING, 0);
				//glfwSetWindowAttrib(window, GLFW_DECORATED, 1);

				glfwSetWindowMonitor(window, nullptr, lastX, lastY, lastWidth, lastHeight, GLFW_DONT_CARE);
				glfwSwapInterval(1);
			}
		}

		if (!editorFileDrops.empty()) {
			glfwFocusWindow(window);
		}

		if (ImGui::GetMousePos().x > -9000) {
			for (auto& file : editorFileDrops) {
				g_project.handleFileDrop(file);
			}
			editorFileDrops.clear();
		}
	}

	// Cleanup
	ImGui_ImplGlfwGL3_Shutdown();
	glfwTerminate();

	FileWatcher::stop();

	return 0;
}
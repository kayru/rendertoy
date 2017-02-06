#pragma once
#include "Common.h"
//#include "StringUtil.h"
#include "Texture.h"
//#include "FileUtil.h"

//#include <glad/glad.h>
#include <unordered_map>
//#include <fstream>
#include <string>


std::vector<char> loadShaderSource(const std::string& path, const char* preprocessorOptions);

struct ParamAnnotation
{
	std::unordered_map<std::string, std::string> items;

	const char* get(const std::string& value, const char* def) const {
		auto it = items.find(value);
		if (it != items.end()) {
			return it->second.c_str();
		}
		else {
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

enum class ShaderParamType {
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

struct ShaderParamValue {
	ShaderParamValue() {
		int4Value = ivec4(0);
	}

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

	TextureDesc textureValue;

	void assign(ShaderParamType type, const ShaderParamValue& other) {
		float4Value = other.float4Value;
		textureValue = other.textureValue;
	}
};

struct ShaderParamRefl {
	std::string name;
	ShaderParamType type;
	ParamAnnotation annotation;

	ShaderParamValue defaultValue() const;
};

struct ShaderParamBindingRefl : ShaderParamRefl {
	unsigned int location = -1;
};

struct ComputeShader
{
	std::vector<ShaderParamBindingRefl> m_params;
	std::string m_sourceFile;
	std::string m_errorLog;

	unsigned int m_csHandle = -1;
	unsigned int m_programHandle = -1;

	// incremented every time the shader is dynamically reloaded
	u32 versionId = 0;

	void reflectParams(const std::unordered_map<std::string, ParamAnnotation>& annotations);

	std::unordered_map<std::string, ParamAnnotation> parseAnnotations(const std::vector<char>& source);

	void updateErrorLogFile();

	bool reload();

	ComputeShader() {}
	ComputeShader(const std::string sourceFile)
		: m_sourceFile(sourceFile)
	{
		reload();
	}
};

struct ShaderParamProxy {
	const ShaderParamBindingRefl& refl;
	ShaderParamValue& value;
	const u32 uid;
	const u32 idx;

	ShaderParamProxy* operator*() {
		return this;
	}

	ShaderParamProxy* operator->() {
		return this;
	}
};

struct ShaderParamIterProxy {
	ShaderParamIterProxy() {}
	ShaderParamIterProxy(const ShaderParamIterProxy& other)
		: refls(other.refls)
		, values(other.values)
		, uids(other.uids)
	{}

	ShaderParamIterProxy(
		const std::vector<ShaderParamBindingRefl>& refls,
		std::vector<ShaderParamValue>& values,
		const std::vector<u32>& uids)
		: refls(&refls)
		, values(&values)
		, uids(&uids)
	{
		assert(refls.size() == values.size());
		assert(refls.size() == uids.size());
	}

	struct Iterator : public std::iterator<std::forward_iterator_tag, Iterator> {
		Iterator(ShaderParamIterProxy* cont, size_t i)
			: refls(cont->refls->data())
			, values(cont->values->data())
			, uids(cont->uids->data())
			, i(i)
		{}

		ShaderParamProxy operator*() const {
			return ShaderParamProxy{ refls[i], values[i], uids[i], u32(i) };
		}

		ShaderParamProxy operator->() const {
			return ShaderParamProxy{ refls[i], values[i], uids[i], u32(i) };
		}

		bool operator==(const Iterator& other) const {
			assert(refls == other.refls);
			return i == other.i;
		}

		bool operator!=(const Iterator& other) const {
			assert(refls == other.refls);
			return i != other.i;
		}

		bool operator<(const Iterator& other) const {
			assert(refls == other.refls);
			return i < other.i;
		}

		const Iterator& operator++() { ++i; return *this; }
		Iterator operator++(int) {
			Iterator result = *this; ++(*this); return result;
		}

	private:
		const ShaderParamBindingRefl* refls;
		ShaderParamValue* values;
		const u32* uids;
		size_t i;
	};

	Iterator begin() { return Iterator(this, 0); }
	Iterator end() { return Iterator(this, refls->size()); }

	size_t size() const {
		return refls->size();
	}

	friend struct Iterator;
private:
	const std::vector<ShaderParamBindingRefl>* refls = nullptr;
	std::vector<ShaderParamValue>* values = nullptr;
	const std::vector<u32>* uids = nullptr;
};

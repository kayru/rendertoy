// ImGui - standalone example application for Glfw + OpenGL 3, using programmable pipeline
// If you are new to ImGui, see examples/README.txt and documentation at the top of imgui.cpp.

#include "Common.h"
#include "FileWatcher.h"
#include "Math.h"
#include "NodeGraph.h"
#include "NodeGraphGui.h"
#include "StringUtil.h"
#include "FileUtil.h"
#include "Shader.h"
#include "Texture.h"
#include "OsUtil.h"

#include <imgui.h>
#include "imgui_impl_glfw_gl3.h"
#include <stdio.h>
#define NOMINMAX	// glad.h, I'm not glad.
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <algorithm>


using JsonWriter = rapidjson::PrettyWriter<rapidjson::StringBuffer>;

struct IRenderPass;
std::shared_ptr<IRenderPass> g_editedPass = nullptr;



namespace std {
	template <>
	struct hash<TextureKey>
	{
		size_t operator()(const TextureKey& k) const {
			size_t res = 17;
			res = res * 31u + hash<u32>()(k.width);
			res = res * 31u + hash<u32>()(k.height);
			res = res * 31u + hash<GLenum>()(k.format);
			return res;
		}
	};
}

namespace std {
	template <>
	struct hash<nodegraph::node_handle>
	{
		size_t operator()(const nodegraph::node_handle& k) const {
			return size_t(k.idx) | (size_t(k.fingerprint) << 16);
		}
	};
}


std::unordered_map<TextureKey, shared_ptr<CreatedTexture>> g_transientTextureCache;


struct CompiledImage
{
	shared_ptr<CreatedTexture> tex;
	bool owned = false;

	bool valid() const {
		return tex && tex->texId != 0;
	}

	void release() {
		g_transientTextureCache[tex->key] = tex;
		tex = nullptr;
		owned = false;
	}
};


struct CompiledPass
{
	vector<GLuint> paramLocations;
	vector<CompiledImage> compiledImages;
	ShaderParamIterProxy params;
	ComputeShader* shader = nullptr;

	void render(u32 width, u32 height)
	{
		// TODO: clean up. this is only there for the Output node which doesn't have a shader
		if (!shader) {
			return;
		}

		glUseProgram(shader->m_programHandle);
		u32 imgUnit = 0;
		u32 texUnit = 0;

		for (const auto& param : params) {
			const auto& refl = param.refl;
			const auto& value = param.value;

			if (refl.type == ShaderParamType::Float) {
				glUniform1f(refl.location, value.floatValue);
			}
			else if (refl.type == ShaderParamType::Float2) {
				glUniform2f(refl.location, value.float2Value.x, value.float2Value.y);
			}
			else if (refl.type == ShaderParamType::Float3) {
				glUniform3f(refl.location, value.float3Value.x, value.float3Value.y, value.float3Value.z);
			}
			else if (refl.type == ShaderParamType::Float4) {
				glUniform4f(refl.location, value.float4Value.x, value.float4Value.y, value.float4Value.z, value.float4Value.w);
			}
			else if (refl.type == ShaderParamType::Int) {
				glUniform1i(refl.location, value.intValue);
			}
			else if (refl.type == ShaderParamType::Int2) {
				glUniform2i(refl.location, value.int2Value.x, value.int2Value.y);
			}
			else if (refl.type == ShaderParamType::Int3) {
				glUniform3i(refl.location, value.int3Value.x, value.int3Value.y, value.int3Value.z);
			}
			else if (refl.type == ShaderParamType::Int4) {
				glUniform4i(refl.location, value.int4Value.x, value.int4Value.y, value.int4Value.z, value.int4Value.w);
			}
			else if (refl.type == ShaderParamType::Image2d) {
				CompiledImage& img = compiledImages[param.idx];
				if (img.valid()) {
					const GLint level = 0;
					const GLenum layered = GL_FALSE;
					glBindImageTexture(imgUnit, img.tex->texId, level, layered, 0, GL_READ_ONLY, GL_RGBA16F);
					glUniform1i(refl.location, imgUnit);
					++imgUnit;
				}
			}
			else if (refl.type == ShaderParamType::Sampler2d) {
				CompiledImage& img = compiledImages[param.idx];
				if (img.valid()) {
					const GLint level = 0;
					const GLenum layered = GL_FALSE;
					glActiveTexture(GL_TEXTURE0 + texUnit);
					glBindTexture(GL_TEXTURE_2D, img.tex->texId);
					glUniform1i(refl.location, texUnit);

					const GLuint samplerId = img.tex->samplerId;
					glSamplerParameteri(samplerId, GL_TEXTURE_WRAP_S, value.textureValue.wrapS ? GL_REPEAT : GL_CLAMP_TO_EDGE);
					glSamplerParameteri(samplerId, GL_TEXTURE_WRAP_T, value.textureValue.wrapT ? GL_REPEAT : GL_CLAMP_TO_EDGE);
					glBindSampler(texUnit, samplerId);
					++texUnit;
				}
			}
		}

		GLint workGroupSize[3];
		glGetProgramiv(shader->m_programHandle, GL_COMPUTE_WORK_GROUP_SIZE, workGroupSize);
		glDispatchCompute(
			(width + workGroupSize[0] - 1) / workGroupSize[0],
			(height + workGroupSize[1] - 1) / workGroupSize[1],
			1);
	}
};

shared_ptr<CreatedTexture> createTransientTexture(const TextureDesc& desc, const TextureKey& key)
{
	auto existing = g_transientTextureCache.find(key);
	if (existing != g_transientTextureCache.end()) {
		auto res = existing->second;
		g_transientTextureCache.erase(existing);
		return res;
	}
	else {
		return createTexture(desc, key);
	}
}


struct PassCompilerSettings
{
	ivec2 windowSize;
};

struct IRenderPass
{
	virtual ~IRenderPass() {}
	virtual ShaderParamIterProxy params() = 0;
	virtual bool compile(const PassCompilerSettings& settings, CompiledPass *const compiled) = 0;
	virtual int findParamByPortUid(nodegraph::port_uid uid) const = 0;
	virtual std::string getDisplayName() const = 0;
	virtual bool canBeRemoved() const = 0;
	virtual void serialize(JsonWriter& writer) = 0;
	virtual void deserialize(rapidjson::Value& json) = 0;

	static u32 nextParamUid() {
		static u32 i = 0;
		return ++i;
	}
};

// Create or load the image
bool compileImage(const PassCompilerSettings& settings, IRenderPass& pass, const TextureDesc& desc, CompiledImage *const compiled, const CompiledPass *const compiledPass)
{
	if (desc.source == TextureDesc::Source::Create) {
		TextureKey key = { 1, 1, GL_RGBA16F };
		if (desc.useRelativeScale) {
			if (desc.scaleRelativeTo == "#window") {
				key.width = u32(std::max(0.0f, desc.relativeScale.x) * settings.windowSize.x);
				key.height = u32(std::max(0.0f, desc.relativeScale.y) * settings.windowSize.y);
			} else {
				u32 otherParamIdx = 0;
				for (const auto& param : pass.params()) {
					if (param.refl.name == desc.scaleRelativeTo) {
						const bool isImage = param.refl.type == ShaderParamType::Sampler2d || param.refl.type == ShaderParamType::Image2d;
						const bool isInputImage = isImage && param.value.textureValue.source != TextureDesc::Source::Create;

						if (isInputImage) {
							auto& otherImg = compiledPass->compiledImages[otherParamIdx].tex;
							if (!otherImg) {
								// TODO: report an error; a required input isn't these, thus we can't compile this graph
								return false;
							}
							key.width = u32(std::max(0.0f, desc.relativeScale.x) * otherImg->key.width);
							key.height = u32(std::max(0.0f, desc.relativeScale.y) * otherImg->key.height);
						} else {
							// TODO: report an error. can only have scale relative to non-created textures
						}
					}

					++otherParamIdx;
				}
			}
		} else {
			key.width = desc.resolution.x;
			key.height = desc.resolution.y;
		}

		key.width = std::max(1u, key.width);
		key.height = std::max(1u, key.height);

		compiled->tex = createTransientTexture(desc, key);
		compiled->owned = true;
	}
	else if (desc.source == TextureDesc::Source::Load) {
		compiled->tex = loadTexture(desc);
	}

	return true;
}

struct OutputPass : IRenderPass
{
	OutputPass()
	{
		ShaderParamBindingRefl param;
		param.name = "image";
		param.type = ShaderParamType::Image2d;
		m_paramRefl.push_back(param);
		ShaderParamValue value;
		value.textureValue.source = TextureDesc::Source::Input;
		m_paramValues.push_back(value);
		m_paramUids.push_back(nextParamUid());
	}

	ShaderParamIterProxy params() override {
		return ShaderParamIterProxy(m_paramRefl, m_paramValues, m_paramUids);
	}

	bool compile(const PassCompilerSettings& settings, CompiledPass *const compiled) override {
		return compileImage(settings, *this, m_paramValues[0].textureValue, &compiled->compiledImages[0], compiled);
	}

	int findParamByPortUid(nodegraph::port_uid uid) const override {
		assert(uid == m_paramUids[0]);
		return 0;
	}

	std::string getDisplayName() const override {
		return "Output";
	}

	bool canBeRemoved() const override {
		return false;
	}

	void serialize(JsonWriter& writer) override
	{
		writer.String("type");
		writer.String("Output");
	}

	void deserialize(rapidjson::Value& json) override
	{
		assert(0 == strcmp(json["type"].GetString(), "Output"));
	}


private:
	vector<ShaderParamBindingRefl> m_paramRefl;
	vector<ShaderParamValue> m_paramValues;
	vector<u32> m_paramUids;
};

struct Pass : IRenderPass
{
	Pass(const std::string& shaderPath)
	{
		m_computeShader = ComputeShader(shaderPath);
		updateParams();

		FileWatcher::watchFile(shaderPath.c_str(), [this]()
		{
			if (m_computeShader.reload()) {
				updateParams();
			}
		});
	}

	~Pass() {
		FileWatcher::stopWatchingFile(m_computeShader.m_sourceFile.c_str());
	}

	ShaderParamIterProxy params() override {
		return ShaderParamIterProxy(m_computeShader.m_params, m_paramValues, m_paramUids);
	}

	const ComputeShader& shader() const {
		return m_computeShader;
	}
 
	bool compile(const PassCompilerSettings& settings, CompiledPass *const compiled) override
	{
		compiled->shader = &m_computeShader;
		compiled->params = params();
		compiled->paramLocations.resize(m_paramRefl.size());

		// Compile Loaded images first, so that we can have Created images relative to their dimensions
		for (size_t i = 0; i < m_paramRefl.size(); ++i) {
			if (m_paramRefl[i].type == ShaderParamType::Image2d && m_paramValues[i].textureValue.source == TextureDesc::Source::Load) {
				if (!compileImage(settings, *this, m_paramValues[i].textureValue, &compiled->compiledImages[i], nullptr)) {
					return false;
				}
			}
		}

		for (size_t i = 0; i < m_paramRefl.size(); ++i) {
			const GLint loc = glGetUniformLocation(m_computeShader.m_programHandle, m_paramRefl[i].name.c_str());
			compiled->paramLocations[i] = loc;

			if (m_paramRefl[i].type == ShaderParamType::Image2d && m_paramValues[i].textureValue.source != TextureDesc::Source::Load) {
				if (!compileImage(settings, *this, m_paramValues[i].textureValue, &compiled->compiledImages[i], compiled)) {
					return false;
				}
			}
		}

		return true;
	}

	int findParamByPortUid(nodegraph::port_uid uid) const override
	{
		for (int i = 0; i < int(m_paramUids.size()); ++i) {
			if (m_paramUids[i] == uid) {
				return i;
			}
		}

		return -1;
	}

	std::string getDisplayName() const override
	{
		std::string filename = fs::path(m_computeShader.m_sourceFile).filename().string();
		return filename.substr(0, filename.find_last_of("."));
	}

	bool canBeRemoved() const override {
		return true;
	}

	void serialize(JsonWriter& writer) override
	{
		writer.String("type");
		writer.String("Compute");

		writer.String("shader");
		writer.String(m_computeShader.m_sourceFile.c_str());
	}

	void deserialize(rapidjson::Value& json) override
	{
		assert(0 == strcmp(json["type"].GetString(), "Compute"));
	}

private:
	void updateParams() {
		vector<ShaderParamValue> newValues(m_computeShader.m_params.size());
		vector<u32> newUids(m_computeShader.m_params.size());

		for (size_t i = 0; i < newValues.size(); ++i) {
			ShaderParamBindingRefl& newRefl = m_computeShader.m_params[i];
			ShaderParamValue& newValue = newValues[i];
			u32& newUid = newUids[i];

			auto curMatch = std::find_if(m_paramRefl.begin(), m_paramRefl.end(), [&](auto& p) { return p.name == newRefl.name; });
			if (curMatch != m_paramRefl.end()) {
				if (curMatch->type == newRefl.type) {
					// Found a value for the new field in the current array
					const size_t src = std::distance(m_paramRefl.begin(), curMatch);
					newValue = m_paramValues[src];
					newUid = m_paramUids[src];
				} else {
					// Otherwise we found the param by name, but the type changed. Use the default.
					newValue = m_computeShader.m_params[i].defaultValue();
					newUid = nextParamUid();
				}

				// Drop the saved param since we have a new entry for it. We'll nuke params with empty names.
				curMatch->name.clear();
			} else {
				// No match in current params, but maybe we have a match in the m_prevParams array.

				auto prevMatch = std::find_if(m_prevParams.begin(), m_prevParams.end(), [&](auto& p) { return p.refl.name == newRefl.name; });
				if (prevMatch != m_prevParams.end()) {
					// Got a match in old params
					if (prevMatch->refl.type == newRefl.type) {
						// Type matches, let's go with it
						newValue = prevMatch->value;
						newUid = prevMatch->uid;
					} else {
						// Otherwise we have found an old param, but its type is now different. Use the default.
						newValue = m_computeShader.m_params[i].defaultValue();
						newUid = nextParamUid();
					}

					// Drop the old param
					prevMatch->refl.name.clear();
				} else {
					// No match found anywhere. Just go with the default.
					newValue = m_computeShader.m_params[i].defaultValue();
					newUid = nextParamUid();
				}
			}
		}

		// Nuke old and current params that we've matched up to the new shader
		m_prevParams.erase(
			std::remove_if(m_prevParams.begin(), m_prevParams.end(), [](const auto& p) { return p.refl.name.empty(); }),
			m_prevParams.end()
		);

		// All params from the previous shader version that we didn't find in the current one
		// go to the m_prevParams array, so that we can restore old values upon further shader modifications.
		for (size_t i = 0; i < m_paramRefl.size(); ++i) {
			if (!m_paramRefl[i].name.empty()) {
				m_prevParams.push_back({ m_paramRefl[i], m_paramValues[i], m_paramUids[i] });
			}
		}

		newValues.swap(m_paramValues);
		newUids.swap(m_paramUids);
		m_paramRefl.resize(m_computeShader.m_params.size());

		for (size_t i = 0; i < m_paramRefl.size(); ++i) {
			m_paramRefl[i] = m_computeShader.m_params[i];
		}
	}

	ComputeShader m_computeShader;
	vector<ShaderParamValue> m_paramValues;
	vector<u32> m_paramUids;

	// Kept around for preserving previous values across shader reload and shader modifications
	vector<ShaderParamRefl> m_paramRefl;
	struct PrevShaderParam {
		ShaderParamRefl refl;
		ShaderParamValue value;
		u32 uid;
	};
	vector<PrevShaderParam> m_prevParams;
};

bool needsOutputPort(const ShaderParamProxy& param)
{
	return param.refl.type == ShaderParamType::Image2d && param.value.textureValue.source == TextureDesc::Source::Create;
}

bool needsInputPort(const ShaderParamProxy& param)
{
	return param.refl.type == ShaderParamType::Image2d && param.value.textureValue.source == TextureDesc::Source::Input;
}

void serializeGraph(nodegraph::Graph& graph, JsonWriter& writer)
{
	writer.String("nodes");
	writer.StartArray();

	graph.iterNodes([&](nodegraph::node_handle nodeHandle) {
		writer.StartObject();

		writer.String("idx");
		writer.Int(nodeHandle.idx);

		writer.String("inputs");
		writer.StartArray();
		graph.iterNodeInputPorts(nodeHandle, [&](nodegraph::port_handle portHandle) {
			writer.StartObject();

			writer.String("idx");
			writer.Int(portHandle.idx);

			const nodegraph::Port& port = graph.ports[portHandle.idx];
			writer.String("uid");
			writer.Int(port.uid);

			if (port.link != nodegraph::invalid_link_idx) {
				writer.String("link");
				writer.Int(port.link);
			}

			writer.EndObject();
		});
		writer.EndArray();

		writer.String("outputs");
		writer.StartArray();
		graph.iterNodeOutputPorts(nodeHandle, [&](nodegraph::port_handle portHandle) {
			writer.StartObject();

			writer.String("idx");
			writer.Int(portHandle.idx);

			const nodegraph::Port& port = graph.ports[portHandle.idx];
			writer.String("uid");
			writer.Int(port.uid);

			writer.EndObject();
		});
		writer.EndArray();

		writer.EndObject();
	});

	writer.EndArray();
}

void deserializeGraph(nodegraph::Graph *const graph, const rapidjson::Value& json, const std::unordered_map<int, nodegraph::node_handle>& nodeMap)
{
	// TODO: this needs to be a bit more involved. need to serialize shader param reflection info,
	// and map UIDs to current ones. The serialized ones will not be valid in the new session

	/*auto& nodes = json["nodes"].GetArray();
	std::unordered_map<int, nodegraph::port_handle> portMap;

	for (size_t i = 0; i < nodes.Size(); ++i) {
		auto& node = nodes[i];
		auto foundNode = nodeMap.find(node["idx"].GetInt());
		if (foundNode == nodeMap.end()) {
			continue;
		}

		const nodegraph::node_handle nodeHandle = foundNode->second;

		auto& inputs = node["inputs"].GetArray();
		for (size_t j = 0; j < inputs.Size(); ++j) {
			auto& port = inputs[j];
			nodegraph::port_handle portHandle = graph->addPort(nodeHandle.idx, port["uid"].GetInt());
			portMap[port["idx"].GetInt()] = portHandle;
		}

		auto& outputs = node["outputs"].GetArray();
		for (size_t j = 0; j < outputs.Size(); ++j) {
			auto& port = outputs[j];
			nodegraph::port_handle portHandle = graph->addPort(nodeHandle.idx, port["uid"].GetInt());
			portMap[port["idx"].GetInt()] = portHandle;
		}
	}*/
}

struct CompiledPackage
{
	vector<CompiledPass> orderedPasses;
	shared_ptr<CreatedTexture> outputTexture;
};

struct Package
{
	vector<shared_ptr<IRenderPass>> m_passes;
	nodegraph::Graph graph;

	nodegraph::node_handle addOutputPass() {
		return addPass(make_shared<OutputPass>());
	}

	void deletePass(u32 passIndex) {
		m_passes[passIndex] = nullptr;
	}

	void getNodeDesc(IRenderPass& pass, nodegraph::NodeDesc *const desc)
	{
		desc->inputs.clear();
		desc->outputs.clear();

		for (auto& p : pass.params()) {
			if (needsInputPort(p)) {
				desc->inputs.push_back(p.uid);
			} else if (needsOutputPort(p)) {
				desc->outputs.push_back(p.uid);
			}
		}
	}

	void updateGraph()
	{
		graph.iterNodes([&](nodegraph::node_handle nodeHandle)
		{
			IRenderPass& pass = *m_passes[nodeHandle.idx];
			nodegraph::NodeDesc desc;
			getNodeDesc(pass, &desc);
			graph.updateNode(nodeHandle, desc);
		});
	}

	void handleFileDrop(const std::string& path)
	{
		if (ends_with(path, ".glsl")) {
			addPass(make_shared<Pass>(path));
		}
	}

	nodegraph::node_handle getOutputPass()
	{
		nodegraph::node_handle result;

		graph.iterNodes([&](nodegraph::node_handle nodeHandle) {
			if (graph.nodes[nodeHandle.idx].firstOutputPort == nodegraph::invalid_port_idx) {
				result = nodeHandle;
			}
		});

		return result;
	}

	void findPassOrder(nodegraph::node_handle outputPass, vector<nodegraph::node_idx> *const order)
	{
		vector<bool> visited(graph.nodes.size(), false);

		std::queue<nodegraph::node_idx> nodeq;
		nodeq.push(outputPass.idx);
		visited[outputPass.idx] = true;

		while (!nodeq.empty()) {
			nodegraph::node_idx nodeIdx = nodeq.back();
			nodeq.pop();
			order->push_back(nodeIdx);

			// TODO: only follow valid links, return error if not all ports are connected
			graph.iterNodeIncidentLinks(nodeIdx, [&](nodegraph::link_handle linkHandle) {
				nodegraph::node_idx srcNode = graph.ports[graph.links[linkHandle.idx].srcPort].node;
				if (!visited[srcNode]) {
					visited[srcNode] = true;
					nodeq.push(srcNode);
				}
			});
		}

		std::reverse(order->begin(), order->end());
	}

	bool compile(const PassCompilerSettings& settings, CompiledPackage *const compiled) {
		u32 alivePassCount = 0;
		graph.iterNodes([&](nodegraph::node_handle) {
			++alivePassCount;
		});

		compiled->orderedPasses.clear();

		// Find the output pass
		nodegraph::node_handle outputPass = getOutputPass();
		if (!outputPass.valid()) {
			return false;
		}

		// Perform a topological sort, and identify the order to run passes in
		vector<nodegraph::node_idx> passOrder;
		findPassOrder(outputPass, &passOrder);

		compiled->orderedPasses.resize(passOrder.size());
		vector<CompiledPass*> passToCompiledPass(m_passes.size(), nullptr);

		// Compile passes, create and load textures
		u32 compiledPassIdx = 0;
		for (const nodegraph::node_idx nodeIdx : passOrder) {
			IRenderPass& dstPass = *m_passes[nodeIdx];
			CompiledPass& dstCompiled = compiled->orderedPasses[compiledPassIdx++];
			passToCompiledPass[nodeIdx] = &dstCompiled;

			dstCompiled.compiledImages.clear();
			dstCompiled.compiledImages.resize(dstPass.params().size());

			// Propagate texture inputs
			graph.iterNodeIncidentLinks(nodeIdx, [&](nodegraph::link_handle linkHandle) {
				const nodegraph::Link& link = graph.links[linkHandle.idx];
				IRenderPass& srcPass = *m_passes[graph.ports[link.srcPort].node];
				CompiledPass& srcCompiled = *passToCompiledPass[graph.ports[link.srcPort].node];

				const nodegraph::Port& srcPort = graph.ports[link.srcPort];
				const nodegraph::Port& dstPort = graph.ports[link.dstPort];

				const int srcParamIdx = srcPass.findParamByPortUid(srcPort.uid);
				const int dstParamIdx = dstPass.findParamByPortUid(dstPort.uid);

				if (srcParamIdx != -1 && dstParamIdx != -1) {
					dstCompiled.compiledImages[dstParamIdx].tex = srcCompiled.compiledImages[srcParamIdx].tex;
				}
			});

			if (!dstPass.compile(settings, &dstCompiled)) {
				return false;
			}
		}

		compiled->outputTexture = nullptr;
		for (auto& img : passToCompiledPass[outputPass.idx]->compiledImages) {
			if (img.valid()) {
				compiled->outputTexture = img.tex;
				break;
			}
		}

		return true;
	}

	void serialize(JsonWriter& writer)
	{
		writer.String("passes");
		writer.StartArray();
		graph.iterNodes([&](nodegraph::node_handle nodeHandle){
			writer.StartObject();

			writer.String("idx");
			writer.Int(nodeHandle.idx);

			m_passes[nodeHandle.idx]->serialize(writer);

			writer.EndObject();
		});
		writer.EndArray();

		writer.String("graph");
		writer.StartObject();
		serializeGraph(graph, writer);
		writer.EndObject();
	}

	void reset()
	{
		resetNodeGraphGui(graph);
		graph = nodegraph::Graph();
		m_passes.clear();
	}

	nodegraph::node_handle deserializeNode(rapidjson::Value& json)
	{
		shared_ptr<IRenderPass> pass;
		const char* const nodeType = json["type"].GetString();

		if (0 == strcmp(nodeType, "Output")) {
			pass = make_shared<OutputPass>();
		} else if (0 == strcmp(nodeType, "Compute")) {
			pass = make_shared<Pass>(json["shader"].GetString());
		} else {
			assert(false);
		}

		pass->deserialize(json);
		return addPass(pass);
	}

	void deserialize(rapidjson::Document& doc, std::unordered_map<int, nodegraph::node_handle> *const nodeMap)
	{
		auto& passArray = doc["passes"];
		const size_t passCount = passArray.Size();

		for (size_t i = 0; i < passCount; ++i ) {
			auto& node = passArray[i];
			const int idx = node["idx"].GetInt();

			nodegraph::node_handle nodeHandle = deserializeNode(node);
			(*nodeMap)[idx] = nodeHandle;
		}
	
		deserializeGraph(&graph, doc["graph"], *nodeMap);
	}

private:

	nodegraph::node_handle addPass(shared_ptr<IRenderPass> pass)
	{
		nodegraph::NodeDesc desc;
		getNodeDesc(*pass, &desc);
		nodegraph::node_handle nodeHandle = graph.addNode(desc);

		if (m_passes.size() == nodeHandle.idx) {
			m_passes.emplace_back(pass);
		}
		else {
			m_passes[nodeHandle.idx] = pass;
		}

		return nodeHandle;
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

void doTextureLoadUi(ShaderParamValue& value)
{
	if (ImGui::Button("Browse...")) {
		openFileDialog("Select an image", "Image Files\0*.exr\0", &value.textureValue.path);
	}

	ImGui::SameLine();
	ImGui::Text(value.textureValue.path.c_str());
}

void doPassUi(Pass& pass)
{
	int maxLabelWidth = 0;
	for (auto& param : pass.params()) {
		maxLabelWidth = std::max(maxLabelWidth, (int)ImGui::CalcTextSize(param.refl.name.c_str()).x);
	}
	maxLabelWidth += 10;

	for (auto& param : pass.params()) {
		const auto& refl = param.refl;
		auto& value = param.value;

		ImGui::PushID(refl.name.c_str());
		ImGui::Columns(2);
		{
			const int textWidth = (int)ImGui::CalcTextSize(refl.name.c_str()).x;
			ImGui::SetCursorPosX(maxLabelWidth - textWidth);
			ImGui::Text(refl.name.c_str());
		}

		ImGui::SetColumnOffset(1, maxLabelWidth + 10);
		ImGui::NextColumn();

		if (refl.type == ShaderParamType::Float) {
			ImGui::SliderFloat("", &value.floatValue, refl.annotation.get("min", 0.0f), refl.annotation.get("max", 1.0f));
		} else if (refl.type == ShaderParamType::Float2) {
			ImGui::SliderFloat2("", &value.float2Value.x, refl.annotation.get("min", 0.0f), refl.annotation.get("max", 1.0f));
		} else if (refl.type == ShaderParamType::Float3) {
			if (refl.annotation.has("color")) {
				ImGui::ColorEdit3("", &value.float3Value.x);
			} else {
				ImGui::SliderFloat3("", &value.float3Value.x, refl.annotation.get("min", 0.0f), refl.annotation.get("max", 1.0f));
			}
		} else if (refl.type == ShaderParamType::Float4) {
			if (refl.annotation.has("color")) {
				ImGui::ColorEdit4("", &value.float4Value.x);
			} else {
				ImGui::SliderFloat4("", &value.float4Value.x, refl.annotation.get("min", 0.0f), refl.annotation.get("max", 1.0f));
			}
		} else if (refl.type == ShaderParamType::Int) {
			ImGui::SliderInt("", &value.intValue, refl.annotation.get("min", 0), refl.annotation.get("max", 16));
		} else if (refl.type == ShaderParamType::Int2) {
			ImGui::SliderInt2("", &value.int2Value.x, refl.annotation.get("min", 0), refl.annotation.get("max", 16));
		} else if (refl.type == ShaderParamType::Int3) {
			ImGui::SliderInt3("", &value.int3Value.x, refl.annotation.get("min", 0), refl.annotation.get("max", 16));
		} else if (refl.type == ShaderParamType::Int4) {
			ImGui::SliderInt4("", &value.int4Value.x, refl.annotation.get("min", 0), refl.annotation.get("max", 16));
		} else if (refl.type == ShaderParamType::Sampler2d) {
			{
				ImGui::PushID("wrapS");
				int wrapS = value.textureValue.wrapS ? 0 : 1;
				const char* const wrapSValues[] = {
					"Wrap S",
					"Clamp S",
				};
				ImGui::PushItemWidth(100);
				ImGui::Combo("", &wrapS, wrapSValues, sizeof(wrapSValues) / sizeof(*wrapSValues));
				value.textureValue.wrapS = !wrapS;
				ImGui::PopID();
			}

			ImGui::SameLine();

			{
				ImGui::PushID("wrapT");
				int wrapT = value.textureValue.wrapT ? 0 : 1;
				const char* const wrapTValues[] = {
					"Wrap T",
					"Clamp T",
				};
				ImGui::PushItemWidth(100);
				ImGui::Combo("", &wrapT, wrapTValues, sizeof(wrapTValues) / sizeof(*wrapTValues));
				value.textureValue.wrapT = !wrapT;
				ImGui::PopID();
			}

			ImGui::SameLine();
			doTextureLoadUi(value);
		} else if (refl.type == ShaderParamType::Image2d) {
			ImGui::BeginGroup();
			int sourceIdx = int(value.textureValue.source);
			const char* const sources[] = {
				"Load",
				"Create",
				"Input",
			};
			ImGui::PushID("source");
			ImGui::PushItemWidth(100);
			bool selected = ImGui::Combo("", &sourceIdx, sources, sizeof(sources) / sizeof(*sources));
			ImGui::PopID();
			const auto prevSource = value.textureValue.source;
			value.textureValue.source = TextureDesc::Source(sourceIdx);

			if (TextureDesc::Source::Load == value.textureValue.source) {
				ImGui::SameLine();
				doTextureLoadUi(value);
			} else if (TextureDesc::Source::Create == value.textureValue.source) {
				int formatIdx = 0;
				const char* const formats[] = {
					"rgba16f",
					"r11g11b10f",
				};

				ImGui::SameLine();
				ImGui::PushItemWidth(100);
				ImGui::Combo("", &formatIdx, formats, sizeof(formats)/sizeof(*formats));

				ImGui::SameLine();
				bool relativeSize = value.textureValue.useRelativeScale;
				ImGui::Checkbox("relative", &relativeSize);
				value.textureValue.useRelativeScale = relativeSize;

				if (relativeSize) {
					ImGui::PushID("relativeSize");
					ImGui::PushItemWidth(100);
					ImGui::SameLine();
					ImGui::InputFloat2("scale", &value.textureValue.relativeScale.x, 2);
					ImGui::SameLine();

					static vector<const char*> targetNames;
					targetNames = { "#window" };

					int targetIdx = 0;

					for (auto& otherParam : pass.params()) {
						if (otherParam.refl.type == ShaderParamType::Image2d && otherParam.value.textureValue.source != TextureDesc::Source::Create) {
							if (otherParam.refl.name == value.textureValue.scaleRelativeTo) {
								targetIdx = int(targetNames.size());
							}
							targetNames.push_back(otherParam.refl.name.c_str());
						}
					}

					int sizeNeeded = 0;
					for (const char* str : targetNames) {
						sizeNeeded = std::max(sizeNeeded, (int)ImGui::CalcTextSize(str).x);
					}
					sizeNeeded += 32;

					ImGui::PushItemWidth(sizeNeeded);
					ImGui::SameLine();
					ImGui::Combo("", &targetIdx, targetNames.data(), targetNames.size());
					ImGui::PopID();

					value.textureValue.scaleRelativeTo = targetNames[targetIdx];
				} else {
					ImGui::PushItemWidth(100);
					ImGui::SameLine();
					ImGui::InputInt2("resolution", &value.textureValue.resolution.x);
				}
			}

			ImGui::EndGroup();
		}

		ImGui::Columns(1);
		ImGui::PopID();
	}

	if (ImGui::Button("Edit shader")) {
		shellExecute(pass.shader().m_sourceFile.c_str());
	}

	if (!pass.shader().m_errorLog.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.2, 0.1, 1));
		ImGui::Text("Compile error:\n%s", pass.shader().m_errorLog.c_str());
		ImGui::PopStyleColor();
	}
}

void doPassUi(IRenderPass& pass)
{
	if (auto p = dynamic_cast<Pass*>(&pass)) {
		doPassUi(*p);
	}
}

static void windowErrorCallback(int error, const char* description)
{
	fprintf(stderr, "Error %d: %s\n", error, description);
}

vector<std::string> editorFileDrops;

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


struct NodeGraphGuiGlue : INodeGraphGuiGlue
{
	vector<std::string> nodeNames;
	vector<vec2> nodePositions;
	vector<PortInfo> portInfo;
	nodegraph::node_handle triggeredNode;
	std::unordered_map<nodegraph::node_handle, vec2> desiredNodePositions;

	void updateInfoFromPackage(Package& package)
	{
		nodegraph::Graph& graph = package.graph;
		nodeNames.resize(graph.nodes.size());
		portInfo.resize(graph.ports.size());
		nodePositions.resize(graph.nodes.size());
		triggeredNode = nodegraph::node_handle();

		graph.iterNodes([&](nodegraph::node_handle nodeHandle)
		{
			IRenderPass& pass = *package.m_passes[nodeHandle.idx];
			nodeNames[nodeHandle.idx] = pass.getDisplayName();

			graph.iterNodeInputPorts(nodeHandle, [&](nodegraph::port_handle portHandle) {
				const nodegraph::Port& port = graph.ports[portHandle.idx];
				auto param = std::find_if(pass.params().begin(), pass.params().end(), [&](auto param) {
					return param.uid == port.uid;
				});

				if (param != pass.params().end()) {
					if (needsInputPort(*param)) {
						portInfo[portHandle.idx] = PortInfo{ param->refl.name, true };
					}
					else {
						// This parameter should not be exposed anymore
						graph.removePort(portHandle);
					}
				}
				else {
					portInfo[portHandle.idx].valid = false;
				}
			});

			graph.iterNodeOutputPorts(nodeHandle, [&](nodegraph::port_handle portHandle) {
				const nodegraph::Port& port = graph.ports[portHandle.idx];
				auto param = std::find_if(pass.params().begin(), pass.params().end(), [&](auto param) {
					return param.uid == port.uid;
				});
				if (param != pass.params().end()) {
					if (needsOutputPort(*param)) {
						portInfo[portHandle.idx] = PortInfo{ param->refl.name, true };
					}
					else {
						// This parameter should not be exposed anymore
						graph.removePort(portHandle);
					}
				}
				else {
					portInfo[portHandle.idx].valid = false;
				}
			});
		});
	}

	std::string getNodeName(nodegraph::node_handle h) const override
	{
		return nodeNames[h.idx];
	}

	PortInfo getPortInfo(nodegraph::port_handle h) const override
	{
		return portInfo[h.idx];
	}

	void onContextMenu() override
	{
		vector<std::string> items;
		getGlobalContextMenuItems(&items);

		for (std::string& it : items) {
			if (ImGui::MenuItem(it.c_str(), NULL, false, true)) {
				onGlobalContextMenuSelected(it);
			}
		}
	}

	void getGlobalContextMenuItems(vector<std::string> *const items)
	{
		vector<fs::path> files;
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

	void onTriggered(nodegraph::node_handle node) override {
		triggeredNode = node;
	}

	bool onRemoveNode(nodegraph::node_handle node) override {
		Package& package = *g_project.m_packages[0];	// HACK
		if (package.m_passes[node.idx]->canBeRemoved()) {
			package.deletePass(node.idx);
			return true;
		}
		else {
			return false;
		}
	}

	bool getNodeDesiredPosition(nodegraph::node_handle nodeHandle, float *const x, float *const y) const override
	{
		auto found = desiredNodePositions.find(nodeHandle);
		if (found != desiredNodePositions.end())  {
			*x = found->second.x;
			*y = found->second.y;
			return true;
		} else {
			return false;
		}
	}

	void updateNodePosition(nodegraph::node_handle nodeHandle, float x, float y) override
	{
		nodePositions[nodeHandle.idx] = vec2(x, y);
		desiredNodePositions.erase(nodeHandle);
	}

	void serialize(JsonWriter& writer)
	{
		writer.String("nodes");
		writer.StartArray();
		
		for (size_t i = 0; i < nodePositions.size(); ++i) {
			writer.StartObject();

			writer.String("idx");
			writer.Int(i);

			writer.String("pos");
			writer.StartArray();
			writer.Double(nodePositions[i].x);
			writer.Double(nodePositions[i].y);
			writer.EndArray();

			writer.EndObject();
		}

		writer.EndArray();
	}

	void deserialize(rapidjson::Value& json, const std::unordered_map<int, nodegraph::node_handle>& nodeMap)
	{
		auto& nodes = json["nodes"].GetArray();
		for (size_t i = 0; i < nodes.Size(); ++i) {
			auto& node = nodes[i];
			int idx = node["idx"].GetInt();
			auto handleFound = nodeMap.find(idx);
			if (handleFound != nodeMap.end()) {
				const nodegraph::node_handle nodeHandle = handleFound->second;
				vec2 pos(node["pos"].GetArray()[0].GetFloat(), node["pos"].GetArray()[1].GetFloat());

				setDesiredNodePosition(nodeHandle, pos);
			}
		}
	}

	void setDesiredNodePosition(nodegraph::node_handle node, vec2 pos) {
		desiredNodePositions[node] = pos;
	}
};


GLFWwindow* g_mainWindow = nullptr;
std::queue<WindowEvent> g_windowEvents;
NodeGraphGuiGlue guiGlue;

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
			vector<char> data = loadTextFileZ("rendertoy.state");

			rapidjson::Document doc;
			doc.Parse(data.data(), data.size());

			std::unordered_map<int, nodegraph::node_handle> nodeMap;
			guiGlue = NodeGraphGuiGlue();
			g_project.m_packages[0]->reset();
			g_project.m_packages[0]->deserialize(doc, &nodeMap);

			guiGlue.deserialize(doc["gui"], nodeMap);

			// TODO: read gui stuff
		}
		if (ImGui::MenuItem("Save", nullptr)) {
			rapidjson::StringBuffer sb;
			rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(sb);
			writer.StartObject();

			g_project.m_packages[0]->serialize(writer);

			writer.String("gui");
			writer.StartObject();
			guiGlue.serialize(writer);
			writer.EndObject();

			writer.EndObject();
			std::ofstream("rendertoy.state").write(sb.GetString(), sb.GetLength());
			puts(sb.GetString());
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

void renderProject(int width, int height)
{
	for (shared_ptr<Package>& package : g_project.m_packages) {
		PassCompilerSettings settings;
		settings.windowSize = ivec2(width, height);

		CompiledPackage compiled;
		if (!package->compile(settings, &compiled) || !compiled.outputTexture) {
			continue;
		}

		for (auto& pass : compiled.orderedPasses) {
			int dispatchWidth = width;
			int dispatchHeight = height;

			// TODO: proper dispatch size setting
			// For now, we get the dispatch size from the first output image of the shader
			for (auto& img : pass.compiledImages) {
				if (img.owned) {
					dispatchWidth = img.tex->key.width;
					dispatchHeight = img.tex->key.height;
					break;
				}
			}

			pass.render(dispatchWidth, dispatchHeight);
		}

		drawFullscreenQuad(compiled.outputTexture->texId);

		for (auto& pass : compiled.orderedPasses) {
			for (auto& img : pass.compiledImages) {
				if (img.owned) {
					img.release();
				}
			}
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

int main(int, char**)
/*int CALLBACK WinMain(
	_In_ HINSTANCE hInstance,
	_In_ HINSTANCE hPrevInstance,
	_In_ LPSTR     lpCmdLine,
	_In_ int       nCmdShow
)*/ {
	// Setup window
	glfwSetErrorCallback(&windowErrorCallback);
	FileWatcher::start();

	if (!glfwInit()) {
		return 1;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);

	GLFWmonitor* monitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* vidMode = glfwGetVideoMode(monitor);
	g_mainWindow = glfwCreateWindow(vidMode->width / 2, vidMode->height, "RenderToy", NULL, NULL);
	GLFWwindow*& window = g_mainWindow;

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
	{
		nodegraph::node_handle outputNode = g_project.m_packages.back()->addOutputPass();
		guiGlue.setDesiredNodePosition(outputNode, vec2(vidMode->width / 2 * 0.7, vidMode->height / 2 * 0.5 - 30));
	}

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

		if (!fullscreen && !maximized)
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
				Package& package = *g_project.m_packages[0];
				package.updateGraph();
				guiGlue.updateInfoFromPackage(package);
				nodeGraph(package.graph, guiGlue);

				if (guiGlue.triggeredNode.valid()) {
					g_editedPass = package.m_passes[guiGlue.triggeredNode.idx];
					// TODO: prevent the GUI editor from appearing for Output nodes
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

		const u32 renderHeight = (fullscreen || maximized) ? display_h : display_h / 2;
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

				const GLFWvidmode* mode = glfwGetVideoMode(monitor);
				glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
				glfwSwapInterval(1);
			} else {
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
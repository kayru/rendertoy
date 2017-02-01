#pragma once
#include "Common.h"
#include <string>
#include <vector>

struct FixedString {
	char data[64] = { 0 };
	FixedString() {}
	FixedString(const char* const n) { *this = n; };
	FixedString(const std::string& n) { *this = n; };
	FixedString& operator=(const char* const n);
	FixedString& operator=(const std::string& n);
};

struct NodeImpl;

struct PortInfo
{
	size_t id;
	FixedString name;
};

struct NodeInfo;
struct LinkInfo {
	NodeInfo* srcNode;
	NodeInfo* dstNode;
	size_t srcPort;
	size_t dstPort;
};

struct NodeInfo
{
	FixedString name;
	std::vector<PortInfo> inputs;
	std::vector<PortInfo> outputs;
	NodeImpl* impl = nullptr;

	void getLinks(std::vector<LinkInfo> *const);
};

struct INodeGraphBackend
{
	// Called every frame
	virtual size_t getNodeCount() = 0;
	virtual NodeInfo* getNodeByIdx(size_t idx) = 0;

	// Called by the graph editor upon various actions
	virtual bool canConnect(const LinkInfo&) = 0;
	virtual void onTriggered(const NodeInfo*) = 0;
	virtual void onDeleted(const NodeInfo*) = 0;
	virtual void onContextMenu(const NodeInfo*) = 0;
};

void nodeGraph(INodeGraphBackend *const backend);

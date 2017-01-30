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

struct NodeInfo;
struct LinkInfo {
	NodeInfo* srcNode;
	NodeInfo* dstNode;
	size_t srcPort;
	size_t dstPort;
};

struct NodeGraphPayload {
};

struct PortInfo
{
	size_t id;
	FixedString name;
};

/*
struct INodeInfo
{
virtual void getName(FixedString *n) = 0;
virtual size_t getInputCount() = 0;
virtual size_t getOutputCount() = 0;
virtual void getInput(size_t i, PortInfo *const) = 0;
virtual void getOutput(size_t i, PortInfo *const) = 0;
virtual NodeImpl* getImpl() = 0;
};
*/

struct NodeImpl;
struct NodeInfo
{
	FixedString name;
	std::vector<PortInfo> inputs;
	std::vector<PortInfo> outputs;
	NodeImpl* impl = nullptr;
};

struct INodeGraphBackend
{
	// Called every frame
	virtual size_t getNodeCount() = 0;
	virtual NodeInfo* getNodeByIdx(size_t idx) = 0;

	virtual void getGlobalContextMenuItems(std::vector<std::string> *const items) = 0;

	// Called once to initialize connections
	//virtual void getLinks(std::vector<LinkInfo> *const) = 0;

	virtual bool onConnected(const LinkInfo& l) = 0;
	virtual void onDisconnected(const LinkInfo& l) = 0;
	virtual void onTriggered(const NodeInfo*) = 0;
	virtual void onGlobalContextMenuSelected(const std::string&) = 0;
};

void nodeGraph(INodeGraphBackend *const backend);

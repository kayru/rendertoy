#pragma once
#include "Common.h"
#include "NodeGraph.h"
#include <string>


struct INodeGraphGuiGlue {
	virtual std::string getNodeName(nodegraph::node_handle) const = 0;
	virtual bool getNodeDesiredPosition(nodegraph::node_handle, float *const x, float *const y) const = 0;

	struct PortInfo {
		std::string name;
		bool valid;
	};

	virtual PortInfo getPortInfo(nodegraph::port_handle) const = 0;

	virtual void onContextMenu() = 0;
	virtual void onTriggered(nodegraph::node_handle node) = 0;
	virtual bool onRemoveNode(nodegraph::node_handle node) = 0;
	virtual void updateNodePosition(nodegraph::node_handle, float x, float y) = 0;
};

void resetNodeGraphGui(nodegraph::Graph& graph);
void nodeGraph(nodegraph::Graph& graph, INodeGraphGuiGlue& infoProvider);

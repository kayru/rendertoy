#pragma once
#include "Common.h"
#include <string>
#include <vector>
#include <cassert>

/*struct FixedString {
	char data[64] = { 0 };
	FixedString() {}
	FixedString(const char* const n) { *this = n; };
	FixedString(const std::string& n) { *this = n; };
	FixedString& operator=(const char* const n);
	FixedString& operator=(const std::string& n);
};*/

namespace nodegraph {
	typedef u32 port_uid;
	typedef u16 port_idx;
	typedef u16 link_idx;
	typedef u16 node_idx;

	constexpr port_idx invalid_port_idx = -1;
	constexpr link_idx invalid_link_idx = -1;
	constexpr node_idx invalid_node_idx = -1;

	template <typename idx_type>
	struct handle {
		idx_type idx = -1;
		u16 fingerprint = -1;

		handle() {}
		handle(idx_type idx, u16 fingerprint)
			: idx(idx)
			, fingerprint(fingerprint)
		{}

		bool valid() const {
			return idx != idx_type(-1);
		}

		bool operator==(const handle& other) const {
			return idx == other.idx && fingerprint == other.fingerprint;
		}
	};

	typedef handle<port_idx> port_handle;
	typedef handle<link_idx> link_handle;
	typedef handle<node_idx> node_handle;

	struct Port {
		port_uid uid;
		node_idx node;
		link_idx link = invalid_link_idx;
		port_idx nextInNode = invalid_port_idx;
		port_idx prevInNode = invalid_port_idx;
		u16 fingerprint = 0;
	};

	struct Link {
		port_idx srcPort;
		port_idx dstPort;
		link_idx nextInSrcPort = invalid_link_idx;
		link_idx prevInSrcPort = invalid_link_idx;
		u16 fingerprint = 0;
	};

	struct Node {
		port_idx firstInputPort = invalid_port_idx;
		port_idx firstOutputPort = invalid_port_idx;
		node_idx nextNode = invalid_node_idx;
		node_idx prevNode = invalid_node_idx;
		u16 fingerprint = 0;
	};

	struct NodeDesc
	{
		std::vector<port_uid> inputs;
		std::vector<port_uid> outputs;
	};

	struct LinkDesc {
		port_handle srcPort;
		port_handle dstPort;
	};

	struct Graph {
		std::vector<Port> ports;
		std::vector<Link> links;
		std::vector<Node> nodes;

		node_idx firstLiveNode = invalid_node_idx;

		std::vector<port_handle> deadPorts;
		std::vector<link_handle> deadLinks;
		std::vector<node_handle> deadNodes;

		template <typename Fn>
		void iterLiveNodes(Fn fn) const {
			for (node_idx it = firstLiveNode; it != invalid_node_idx; it = nodes[it].nextNode) {
				fn(node_handle(it, nodes[it].fingerprint));
			}
		}

		template <typename Fn>
		void iterNodeInputPorts(node_handle nodeHandle, Fn fn) const {
			const Node& node = nodes[nodeHandle.idx];
			assert(node.fingerprint == nodeHandle.fingerprint);

			for (port_idx it = node.firstInputPort; it != invalid_port_idx; it = ports[it].nextInNode) {
				fn(port_handle(it, ports[it].fingerprint));
			}
		}

		template <typename Fn>
		void iterNodeOutputPorts(node_handle nodeHandle, Fn fn) const {
			const Node& node = nodes[nodeHandle.idx];
			assert(node.fingerprint == nodeHandle.fingerprint);

			for (port_idx it = node.firstOutputPort; it != invalid_port_idx; it = ports[it].nextInNode) {
				fn(port_handle(it, ports[it].fingerprint));
			}
		}

		node_handle getPortNode(port_handle portHandle) const {
			const node_idx idx = ports[portHandle.idx].node;
			return { idx, nodes[idx].fingerprint };
		}

		port_idx addPort(node_idx node, port_uid uid)
		{
			port_idx idx;
			if (deadPorts.size() > 0) {
				idx = deadPorts.back().idx;
				ports[idx].fingerprint = deadPorts.back().fingerprint + 1;
				deadPorts.pop_back();
			} else {
				idx = port_idx(ports.size());
				ports.push_back(Port());
			}

			Port& port = ports[idx];
			port.node = node;
			port.uid = uid;
			port.link = invalid_link_idx;
			return idx;
		}

		void addInputPortToNode(Node& node, port_idx port)
		{
			ports[port].nextInNode = node.firstInputPort;
			if (node.firstInputPort != invalid_port_idx) ports[node.firstInputPort].prevInNode = port;
			node.firstInputPort = port;
		}

		void addOutputPortToNode(Node& node, port_idx port)
		{
			ports[port].nextInNode = node.firstOutputPort;
			if (node.firstOutputPort != invalid_port_idx) ports[node.firstOutputPort].prevInNode = port;
			node.firstOutputPort = port;
		}

		void addLink(port_idx srcPort, port_idx dstPort)
		{
			// Input ports can only have one link
			if (ports[dstPort].link != invalid_link_idx) {
				removeLink(ports[dstPort].link);
			}

			link_idx idx;
			if (deadLinks.size() > 0) {
				idx = deadLinks.back().idx;
				links[idx].fingerprint = deadLinks.back().fingerprint + 1;
				deadLinks.pop_back();
			}
			else {
				idx = link_idx(links.size());
				links.push_back(Link());
			}

			Link& link = links[idx];
			link.srcPort = srcPort;
			link.dstPort = dstPort;

			// Connect the src links list
			link.nextInSrcPort = ports[srcPort].link;
			if (link.nextInSrcPort != invalid_link_idx) links[link.nextInSrcPort].prevInSrcPort = idx;
			ports[srcPort].link = idx;

			ports[dstPort].link = idx;
		}

		void addLink(const LinkDesc& desc)
		{
			addLink(desc.srcPort.idx, desc.dstPort.idx);
		}

		void removeLink(link_idx idx)
		{
			Link& link = links[idx];
			if (link.nextInSrcPort != invalid_link_idx) links[link.nextInSrcPort].prevInSrcPort = link.prevInSrcPort;
			if (link.prevInSrcPort != invalid_link_idx) {
				links[link.prevInSrcPort].nextInSrcPort = link.nextInSrcPort;
			} else {
				// Update head
				ports[link.srcPort].link = link.nextInSrcPort;
			}

			ports[link.dstPort].link = invalid_link_idx;

			deadLinks.push_back({ idx, links[idx].fingerprint });
		}

		void removePort(port_idx idx)
		{
			Port& port = ports[idx];

			while (port.link != invalid_link_idx) {
				removeLink(port.link);
			}

			if (port.nextInNode != invalid_port_idx) ports[port.nextInNode].prevInNode = port.prevInNode;
			if (port.prevInNode != invalid_port_idx) {
				ports[port.prevInNode].nextInNode = port.nextInNode;
			} else {
				// Update head
				auto& node = nodes[port.node];
				if (node.firstInputPort == idx) node.firstInputPort = port.nextInNode;
				else if (node.firstOutputPort == idx) node.firstOutputPort = port.nextInNode;
			}

			deadPorts.push_back({ idx, ports[idx].fingerprint });
		}

		void removeMissingInputPorts(Node& node, NodeDesc& desc)
		{
			port_idx lastInputPort = node.firstInputPort;
			if (lastInputPort != invalid_port_idx) {
				// Find the last input port
				while (true) {
					port_idx next = ports[lastInputPort].nextInNode;
					if (next != invalid_port_idx) lastInputPort = next;
					else break;
				}

				// Iterate backwards, removing items missing from the new desc
				for (port_idx it = lastInputPort; it != invalid_port_idx; it = ports[it].prevInNode) {
					bool found = false;
					auto portUid = ports[it].uid;

					for (auto& d : desc.inputs) {
						if (d == portUid) {
							found = true;
							break;
						}
					}

					if (!found) {
						removePort(it);
					}
				}
			}
		}

		void removeMissingOutputPorts(Node& node, NodeDesc& desc)
		{
			port_idx lastOutputPort = node.firstOutputPort;
			if (lastOutputPort != invalid_port_idx) {
				// Find the last input port
				while (true) {
					port_idx next = ports[lastOutputPort].nextInNode;
					if (next != invalid_port_idx) lastOutputPort = next;
					else break;
				}

				// Iterate backwards, removing items missing from the new desc
				for (port_idx it = lastOutputPort; it != invalid_port_idx; it = ports[it].prevInNode) {
					bool found = false;
					auto portUid = ports[it].uid;

					for (auto& d : desc.outputs) {
						if (d == portUid) {
							found = true;
							break;
						}
					}

					if (!found) {
						removePort(it);
					}
				}
			}
		}

		void addMissingInputPorts(Node& node, NodeDesc& desc)
		{
			for (u32 descInputIdx = 0; descInputIdx < desc.inputs.size(); ++descInputIdx) {
				const auto uid = desc.inputs[descInputIdx];

				bool found = false;
				for (auto it = node.firstInputPort; it != invalid_port_idx; it = ports[it].nextInNode) {
					if (ports[it].uid == uid) {
						found = true;
						break;
					}
				}

				if (!found) {
					port_idx port = addPort(std::distance(nodes.data(), &node), desc.inputs[descInputIdx]);
					addInputPortToNode(node, port);
				}
			}
		}

		void addMissingOutputPorts(Node& node, NodeDesc& desc)
		{
			for (u32 descOutputIdx = 0; descOutputIdx < desc.outputs.size(); ++descOutputIdx) {
				const auto uid = desc.outputs[descOutputIdx];

				bool found = false;
				for (auto it = node.firstOutputPort; it != invalid_port_idx; it = ports[it].nextInNode) {
					if (ports[it].uid == uid) {
						found = true;
						break;
					}
				}

				if (!found) {
					port_idx port = addPort(std::distance(nodes.data(), &node), desc.outputs[descOutputIdx]);
					addOutputPortToNode(node, port);
				}
			}
		}

		// Public

		void updateNode(node_handle h, NodeDesc& desc)
		{
			Node& node = nodes[h.idx];
			assert(node.fingerprint == h.fingerprint);
			removeMissingInputPorts(node, desc);
			removeMissingOutputPorts(node, desc);
			addMissingInputPorts(node, desc);
			addMissingOutputPorts(node, desc);
		}

		node_handle addNode(NodeDesc& desc)
		{
			node_idx idx;
			if (deadNodes.size() > 0) {
				idx = deadNodes.back().idx;
				nodes[idx].fingerprint = deadNodes.back().fingerprint + 1;
				deadNodes.pop_back();
			}
			else {
				idx = node_idx(nodes.size());
				nodes.push_back(Node());
			}

			Node& node = nodes[idx];
			node.nextNode = firstLiveNode;
			if (firstLiveNode != invalid_node_idx) nodes[firstLiveNode].prevNode = idx;
			firstLiveNode = idx;

			if (desc.inputs.size() > 0) {
				node.firstInputPort = addPort(idx, desc.inputs.back());
				for (int i = desc.inputs.size() - 2; i >= 0; --i) {
					port_idx p = addPort(idx, desc.inputs[i]);
					addInputPortToNode(node, p);
				}
			}
			else {
				node.firstInputPort = invalid_port_idx;
			}

			if (desc.outputs.size() > 0) {
				node.firstOutputPort = addPort(idx, desc.outputs.back());
				for (int i = desc.outputs.size() - 2; i >= 0; --i) {
					port_idx p = addPort(idx, desc.outputs[i]);
					addOutputPortToNode(node, p);
				}
			}
			else {
				node.firstOutputPort = invalid_port_idx;
			}

			return { idx, nodes[idx].fingerprint };
		}
	};
}


/*struct NodeImpl;

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
};*/

struct INodeGraphGuiGlue {
	virtual std::string getNodeName(nodegraph::node_handle) const = 0;
	virtual std::string getPortName(nodegraph::port_handle) const = 0;

	virtual void onContextMenu() = 0;
	virtual void onTriggered(nodegraph::node_handle node) = 0;
};

void nodeGraph(nodegraph::Graph& graph, INodeGraphGuiGlue& infoProvider);

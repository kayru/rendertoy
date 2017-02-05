#pragma once
#include "Common.h"
#include <string>
#include <vector>
#include <cassert>



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
		bool operator!=(const handle& other) const {
			return !(*this == other);
		}
	};

	typedef handle<port_idx> port_handle;
	typedef handle<link_idx> link_handle;
	typedef handle<node_idx> node_handle;

	struct Port {
		port_uid uid = 0;
		node_idx node = invalid_node_idx;
		link_idx link = invalid_link_idx;
		port_idx nextInNode = invalid_port_idx;
		port_idx prevInNode = invalid_port_idx;
		u16 fingerprint = 0;
	};

	struct Link {
		port_idx srcPort = invalid_port_idx;
		port_idx dstPort = invalid_port_idx;
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
		void iterNodes(Fn fn) const {
			node_idx next;
			for (node_idx it = firstLiveNode; it != invalid_node_idx; it = next) {
				next = nodes[it].nextNode;
				fn(node_handle(it, nodes[it].fingerprint));

				// Removal of the same element is not supported during iteration
				assert(next == nodes[it].nextNode);
			}
		}

		template <typename Fn>
		void iterNodeInputPorts(node_idx nodeIdx, Fn fn) const {
			const Node& node = nodes[nodeIdx];

			port_idx next;
			for (port_idx it = node.firstInputPort; it != invalid_port_idx; it = next) {
				next = ports[it].nextInNode;
				fn(port_handle(it, ports[it].fingerprint));

				// Removal of the same element is not supported during iteration
				assert(next == ports[it].nextInNode);
			}
		}

		template <typename Fn>
		void iterNodeInputPorts(node_handle nodeHandle, Fn fn) const {
			const Node& node = nodes[nodeHandle.idx];
			assert(node.fingerprint == nodeHandle.fingerprint);

			iterNodeInputPorts(nodeHandle.idx, fn);
		}

		template <typename Fn>
		void iterNodeOutputPorts(node_handle nodeHandle, Fn fn) const {
			const Node& node = nodes[nodeHandle.idx];
			assert(node.fingerprint == nodeHandle.fingerprint);

			port_idx next;
			for (port_idx it = node.firstOutputPort; it != invalid_port_idx; it = next) {
				next = ports[it].nextInNode;
				fn(port_handle(it, ports[it].fingerprint));

				// Removal of the same element is not supported during iteration
				assert(next == ports[it].nextInNode);
			}
		}

		template <typename Fn>
		void iterOutputPortLinks(port_handle portHandle, Fn fn) const {
			const Port& port = ports[portHandle.idx];
			assert(port.fingerprint == portHandle.fingerprint);

			link_idx next;
			for (link_idx it = port.link; it != invalid_link_idx; it = next) {
				next = links[it].nextInSrcPort;
				fn(link_handle(it, links[it].fingerprint));

				// Removal of the same element is not supported during iteration
				assert(next == links[it].nextInSrcPort);
			}
		}

		template <typename Fn>
		void iterNodeIncidentLinks(node_idx nodeIdx, Fn fn) const {
			iterNodeInputPorts(nodeIdx, [&](nodegraph::port_handle portHandle) {
				const Port& dstPort = ports[portHandle.idx];
				if (dstPort.link != invalid_link_idx) {
					fn(link_handle(dstPort.link, links[dstPort.link].fingerprint));
				}
			});
		}

		template <typename Fn>
		void iterNodeIncidentLinks(node_handle nodeHandle, Fn fn) const {
			iterNodeIncidentLinks(nodeHandle.idx, fn);
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
			links[idx] = Link();
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
			ports[idx] = Port();
		}

		void removeNode(node_handle nodeHandle)
		{
			Node& node = nodes[nodeHandle.idx];
		
			while (node.firstInputPort != invalid_port_idx) {
				removePort(node.firstInputPort);
			}

			while (node.firstOutputPort != invalid_port_idx) {
				removePort(node.firstOutputPort);
			}

			if (node.nextNode != invalid_node_idx) nodes[node.nextNode].prevNode = node.prevNode;
			if (node.prevNode != invalid_port_idx) {
				nodes[node.prevNode].nextNode = node.nextNode;
			}
			else {
				// Update head
				if (firstLiveNode == nodeHandle.idx) {
					firstLiveNode = node.nextNode;
				}
			}

			deadNodes.push_back({ nodeHandle.idx, node.fingerprint });
			nodes[nodeHandle.idx] = Node();
		}

		void removeUnreferencedInputPorts(Node& node, NodeDesc& desc)
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
					if (ports[it].link == invalid_link_idx) {
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
		}

		void removeUnreferencedOutputPorts(Node& node, NodeDesc& desc)
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
					if (ports[it].link == invalid_link_idx) {
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
			removeUnreferencedInputPorts(node, desc);
			removeUnreferencedOutputPorts(node, desc);
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

		void removePort(port_handle portHandle)
		{
			assert(ports[portHandle.idx].fingerprint == portHandle.fingerprint);
			removePort(portHandle.idx);
		}

		port_handle portHandle(port_idx idx) {
			return port_handle(idx, ports[idx].fingerprint);
		}
	};
}


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

void nodeGraph(nodegraph::Graph& graph, INodeGraphGuiGlue& infoProvider);

#pragma once
#include "Common.h"
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

		node_handle getPortNode(port_handle portHandle) const;
		port_handle addPort(node_idx node, port_uid uid);

		void addInputPortToNode(Node& node, port_idx port);
		void addOutputPortToNode(Node& node, port_idx port);

		void addLink(port_idx srcPort, port_idx dstPort);
		void addLink(const LinkDesc& desc);

		void removeLink(link_idx idx);
		void removePort(port_idx idx);
		void removeNode(node_handle nodeHandle);

		void removeUnreferencedInputPorts(Node& node, NodeDesc& desc);
		void removeUnreferencedOutputPorts(Node& node, NodeDesc& desc);

		void addMissingInputPorts(Node& node, NodeDesc& desc);
		void addMissingOutputPorts(Node& node, NodeDesc& desc);

		// Public

		void updateNode(node_handle h, NodeDesc& desc);
		node_handle addNode(NodeDesc& desc);

		void removePort(port_handle portHandle);
		port_handle portHandle(port_idx idx);
	};
}

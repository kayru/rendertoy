#include "NodeGraph.h"


namespace nodegraph
{
	node_handle Graph::getPortNode(port_handle portHandle) const {
		const node_idx idx = ports[portHandle.idx].node;
		return{ idx, nodes[idx].fingerprint };
	}

	port_handle Graph::addPort(node_idx node, port_uid uid)
	{
		port_idx idx;
		if (deadPorts.size() > 0) {
			idx = deadPorts.back().idx;
			ports[idx].fingerprint = deadPorts.back().fingerprint + 1;
			deadPorts.pop_back();
		}
		else {
			idx = port_idx(ports.size());
			ports.push_back(Port());
		}

		Port& port = ports[idx];
		port.node = node;
		port.uid = uid;
		port.link = invalid_link_idx;
		return{ idx, port.fingerprint };
	}

	void Graph::addInputPortToNode(Node& node, port_idx port)
	{
		ports[port].nextInNode = node.firstInputPort;
		if (node.firstInputPort != invalid_port_idx) ports[node.firstInputPort].prevInNode = port;
		node.firstInputPort = port;
	}

	void Graph::addOutputPortToNode(Node& node, port_idx port)
	{
		ports[port].nextInNode = node.firstOutputPort;
		if (node.firstOutputPort != invalid_port_idx) ports[node.firstOutputPort].prevInNode = port;
		node.firstOutputPort = port;
	}

	void Graph::addLink(port_idx srcPort, port_idx dstPort)
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

	void Graph::addLink(const LinkDesc& desc)
	{
		addLink(desc.srcPort.idx, desc.dstPort.idx);
	}

	void Graph::removeLink(link_idx idx)
	{
		Link& link = links[idx];
		if (link.nextInSrcPort != invalid_link_idx) links[link.nextInSrcPort].prevInSrcPort = link.prevInSrcPort;
		if (link.prevInSrcPort != invalid_link_idx) {
			links[link.prevInSrcPort].nextInSrcPort = link.nextInSrcPort;
		}
		else {
			// Update head
			ports[link.srcPort].link = link.nextInSrcPort;
		}

		ports[link.dstPort].link = invalid_link_idx;
		deadLinks.push_back({ idx, links[idx].fingerprint });
		links[idx] = Link();
	}

	void Graph::removePort(port_idx idx)
	{
		Port& port = ports[idx];

		while (port.link != invalid_link_idx) {
			removeLink(port.link);
		}

		if (port.nextInNode != invalid_port_idx) ports[port.nextInNode].prevInNode = port.prevInNode;
		if (port.prevInNode != invalid_port_idx) {
			ports[port.prevInNode].nextInNode = port.nextInNode;
		}
		else {
			// Update head
			auto& node = nodes[port.node];
			if (node.firstInputPort == idx) node.firstInputPort = port.nextInNode;
			else if (node.firstOutputPort == idx) node.firstOutputPort = port.nextInNode;
		}

		deadPorts.push_back({ idx, ports[idx].fingerprint });
		ports[idx] = Port();
	}

	void Graph::removeNode(node_handle nodeHandle)
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

	void Graph::removeUnreferencedInputPorts(Node& node, NodeDesc& desc)
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

	void Graph::removeUnreferencedOutputPorts(Node& node, NodeDesc& desc)
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

	void Graph::addMissingInputPorts(Node& node, NodeDesc& desc)
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
				port_idx port = addPort(std::distance(nodes.data(), &node), desc.inputs[descInputIdx]).idx;
				addInputPortToNode(node, port);
			}
		}
	}

	void Graph::addMissingOutputPorts(Node& node, NodeDesc& desc)
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
				port_idx port = addPort(std::distance(nodes.data(), &node), desc.outputs[descOutputIdx]).idx;
				addOutputPortToNode(node, port);
			}
		}
	}

	void Graph::updateNode(node_handle h, NodeDesc& desc)
	{
		Node& node = nodes[h.idx];
		assert(node.fingerprint == h.fingerprint);
		removeUnreferencedInputPorts(node, desc);
		removeUnreferencedOutputPorts(node, desc);
		addMissingInputPorts(node, desc);
		addMissingOutputPorts(node, desc);
	}

	node_handle Graph::addNode(NodeDesc& desc)
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
			node.firstInputPort = addPort(idx, desc.inputs.back()).idx;
			for (int i = desc.inputs.size() - 2; i >= 0; --i) {
				port_idx p = addPort(idx, desc.inputs[i]).idx;
				addInputPortToNode(node, p);
			}
		}
		else {
			node.firstInputPort = invalid_port_idx;
		}

		if (desc.outputs.size() > 0) {
			node.firstOutputPort = addPort(idx, desc.outputs.back()).idx;
			for (int i = desc.outputs.size() - 2; i >= 0; --i) {
				port_idx p = addPort(idx, desc.outputs[i]).idx;
				addOutputPortToNode(node, p);
			}
		}
		else {
			node.firstOutputPort = invalid_port_idx;
		}

		return{ idx, nodes[idx].fingerprint };
	}

	void Graph::removePort(port_handle portHandle)
	{
		assert(ports[portHandle.idx].fingerprint == portHandle.fingerprint);
		removePort(portHandle.idx);
	}

	port_handle Graph::portHandle(port_idx idx) {
		return port_handle(idx, ports[idx].fingerprint);
	}
}
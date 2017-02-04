// ImGui - standalone example application for Glfw + OpenGL 3, using programmable pipeline

#include "NodeGraph.h"
#include "FreeList.h"

#include <imgui.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <algorithm>
#include <unordered_map>
#include <stdint.h>

const static ImColor defaultPortColor = ImColor(150, 150, 150, 255);
const static ImColor invalidPortColor = ImColor(255, 32, 8, 255);

const static ImColor defaultPortLabelColor = ImColor(255, 255, 255, 255);
const static ImColor invalidPortLabelColor = ImColor(255, 32, 8, 255);

// NB: You can use math functions/operators on ImVec2 if you #define IMGUI_DEFINE_MATH_OPERATORS and #include "imgui_internal.h"
static inline ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x+rhs.x, lhs.y+rhs.y); }
static inline ImVec2 operator-(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x-rhs.x, lhs.y-rhs.y); }
static inline ImVec2 operator*(const ImVec2& lhs, const float rhs) { return ImVec2(lhs.x*rhs, lhs.y*rhs); }

static float dot(const ImVec2& a, const ImVec2& b) {
	return a.x * b.x + a.y * b.y;
}

static float lengthSquared(const ImVec2& c) {
	return dot(c, c);
}

static float length(const ImVec2& c) {
	return sqrtf(lengthSquared(c));
}

static float distance(const ImVec2& a, const ImVec2& b) {
	return length(a - b);
}

// http://stackoverflow.com/questions/849211/shortest-distance-between-a-point-and-a-line-segment
static float minimumDistance(const ImVec2& v, const ImVec2& w, const ImVec2& p) {
	// Return minimum distance between line segment vw and point p
	const float l2 = lengthSquared(v - w);  // i.e. |w-v|^2 -  avoid a sqrt
	if (l2 == 0.0f) return distance(p, v);  // v == w case
											// Consider the line extending the segment, parameterized as v + t (w - v).
											// We find projection of point p onto the line. 
											// It falls where t = [(p-v) . (w-v)] / |w-v|^2
											// We clamp t from [0,1] to handle points outside the segment vw.
	const float t = std::max(0.0f, std::min(1.0f, dot(p - v, w - v) / l2));
	const ImVec2 projection = v + (w - v) * t;  // Projection falls on the segment
	return distance(p, projection);
}

struct PortState {
	ImVec2 pos;
	bool valid = true;
};

struct NodeState
{
	ImVec2 Pos = { 0, 0 };
	ImVec2 Size = { 0, 0 };
};

enum DragState {
	DragState_Default,
	DragState_DraggingConnected,
	DragState_Dragging,
};

void GetBezierCurvePathVertices(ImVector<ImVec2>* path, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, const ImVec2& p4);

struct BezierCurve {
	ImVec2 pos0;
	ImVec2 cp0;
	ImVec2 cp1;
	ImVec2 pos1;

	float distanceToPoint(const ImVec2& p) {
		static ImVector<ImVec2> verts;
		verts.clear();
		GetBezierCurvePathVertices(&verts, pos0, cp0, cp1, pos1);

		float minDist = 1e10f;
		for (int i = 1; i < verts.size(); ++i) {
			minDist = std::min(minDist, minimumDistance(verts[i-1], verts[i], p));
		}

		return minDist;
	}
};

struct Connector {
	nodegraph::port_handle port;
	bool isOutput;
};

static std::vector<nodegraph::port_handle> s_dragPorts;
static bool s_draggingOutput;
static DragState s_dragState = DragState_Default;

BezierCurve getNodeLinkCurve(const ImVec2& fromPort, const ImVec2& toPort)
{
	ImVec2 connectionVector = toPort - fromPort;
	float curvature = length(ImVec2(connectionVector.x * 0.5f, connectionVector.y * 0.25f));
	return {
		fromPort,
		fromPort + ImVec2(curvature, 0),
		toPort + ImVec2(-curvature, 0),
		toPort
	};
}

void drawNodeLink(ImDrawList *const drawList, const BezierCurve& c, ImColor col = ImColor(200, 200, 100, 128))
{
	drawList->AddBezierCurve(c.pos0, c.cp0, c.cp1, c.pos1, col, 3.0f);
}


struct NodeGraphState
{
	std::vector<NodeState> nodes;
	std::vector<PortState> ports;

	ImVec2 scrolling = ImVec2(0.0f, 0.0f);
	ImVec2 originOffset = ImVec2(0.0f, 0.0f);
	nodegraph::node_handle nodeSelected;

	bool openContextMenu = false;
	nodegraph::node_handle nodeHoveredInScene;

	Connector getHoverCon(const nodegraph::Graph& graph, ImVec2 offset, float maxDist)
	{
		const ImVec2 mousePos = ImGui::GetIO().MousePos;
		Connector result;

		graph.iterNodes([&](nodegraph::node_handle nodeHandle)
		{
			const nodegraph::Node& node = graph.nodes[nodeHandle.idx];

			{
				float closestDist = 1e5f;
				nodegraph::port_handle closest;

				graph.iterNodeInputPorts(nodeHandle, [&](nodegraph::port_handle portHandle)
				{
					const float d = distance(ports[portHandle.idx].pos, mousePos);
					if (d < closestDist) {
						closestDist = d;
						closest = portHandle;
					}
				});

				if (closestDist < maxDist) {
					result = Connector{ closest, false };
					return;
				}
			}

			{
				float closestDist = 1e5f;
				nodegraph::port_handle closest;

				graph.iterNodeOutputPorts(nodeHandle, [&](nodegraph::port_handle portHandle)
				{
					const float d = distance(ports[portHandle.idx].pos, mousePos);
					if (d < closestDist) {
						closestDist = d;
						closest = portHandle;
					}
				});

				if (closestDist < maxDist) {
					result = Connector{ closest, true };
					return;
				}
			}
		});

		return result;
	}

	const float NODE_SLOT_RADIUS = 5.0f;

	void drawNodeConnector(ImDrawList* const drawList, const ImVec2& pos, ImColor col = defaultPortColor)
	{
		drawList->AddCircleFilled(pos, NODE_SLOT_RADIUS, col, 12);
	}

	void stopDragging()
	{
		s_dragState = DragState_Default;
		s_dragPorts.clear();
	}

	ImVec2 getPortPos(nodegraph::port_idx h) const
	{
		return ports[h].pos;
	}

	ImVec2 getPortPos(nodegraph::port_handle h) const
	{
		return ports[h.idx].pos;
	}

	// Must be called after drawNodes
	void updateDragging(nodegraph::Graph& graph, INodeGraphGuiGlue& glue, ImDrawList* const drawList, ImVec2 offset)
	{
		switch (s_dragState)
		{
		case DragState_Default:
		{
			Connector con = getHoverCon(graph, offset, NODE_SLOT_RADIUS * 1.5f);
			if (con.port.valid()) {
				if (ImGui::IsMouseClicked(0)) {
					s_dragPorts.push_back(con.port);
					s_draggingOutput = con.isOutput;

					const bool dragOutInput = !con.isOutput && graph.ports[con.port.idx].link != nodegraph::invalid_link_idx;
					const bool dragOutInvalidOutput = con.isOutput && !ports[con.port.idx].valid;

					if (dragOutInput || dragOutInvalidOutput) {
						s_dragState = DragState_DraggingConnected;
					} else {
						s_dragState = DragState_Dragging;
					}
				}
			}
			break;
		}

		case DragState_DraggingConnected:
		{
			assert(1 == s_dragPorts.size());

			const bool drop = !ImGui::IsMouseDown(0);
			if (drop) {
				stopDragging();
				return;
			}

			Connector con = getHoverCon(graph, offset, NODE_SLOT_RADIUS * 3.f);
			if (!con.port.valid() || s_dragPorts[0] != con.port)
			{
				nodegraph::port_idx detachedPort = s_dragPorts[0].idx;
				s_dragPorts.clear();

				if (!s_draggingOutput) {
					nodegraph::link_idx link = graph.ports[detachedPort].link;
					nodegraph::port_handle srcPort = graph.portHandle(graph.links[link].srcPort);
					s_dragPorts.push_back(srcPort);
					graph.removeLink(link);
				} else {
					nodegraph::link_idx toRemove = nodegraph::invalid_link_idx;
					graph.iterOutputPortLinks(graph.portHandle(detachedPort), [&](nodegraph::port_handle link) {
						nodegraph::port_handle dstPort = graph.portHandle(graph.links[link.idx].dstPort);
						s_dragPorts.push_back(dstPort);

						// Can't remove the current link, but we can remove the previous one.
						if (toRemove != nodegraph::invalid_link_idx) {
							graph.removeLink(toRemove);
						}
						toRemove = link.idx;
					});

					// Nuke the last link if any
					if (toRemove != nodegraph::invalid_link_idx) {
						graph.removeLink(toRemove);
					}
				}

				s_draggingOutput = !s_draggingOutput;
				s_dragState = DragState_Dragging;
			}

			break;
		}

		case DragState_Dragging:
		{
			if (s_draggingOutput) {
				assert(1 == s_dragPorts.size());
				BezierCurve linkCurve = getNodeLinkCurve(getPortPos(s_dragPorts[0]), ImGui::GetIO().MousePos);
				drawNodeLink(drawList, linkCurve);
			} else {
				for (auto port : s_dragPorts) {
					BezierCurve linkCurve = getNodeLinkCurve(ImGui::GetIO().MousePos, getPortPos(port));
					drawNodeLink(drawList, linkCurve);
				}
			}

			const bool drop = !ImGui::IsMouseDown(0);

			Connector con = getHoverCon(graph, offset, NODE_SLOT_RADIUS * 3.f);
			if (con.port.valid())
			{
				nodegraph::node_handle conNode = graph.getPortNode(con.port);

				bool canDropAll = true;
				for (auto dragPort : s_dragPorts) {
					nodegraph::node_handle dragNode = graph.getPortNode(dragPort);
					bool canDrop = false;

					if (conNode.idx != dragNode.idx && con.isOutput != s_draggingOutput) {
						// TODO: more checks

						canDrop = true;
					}

					canDropAll = canDropAll && canDrop;
				}

				if (canDropAll)
				{
					drawList->ChannelsSetCurrent(2);
					drawNodeConnector(drawList, getPortPos(con.port), ImColor(32, 220, 120, 255));

					if (drop)
					{
						// Add all the connections

						for (auto dragPort : s_dragPorts) {
							nodegraph::node_handle dragNode = graph.getPortNode(dragPort);
							nodegraph::LinkDesc li;

							if (s_draggingOutput) {
								li = { dragPort, con.port };
							}
							else {
								li = { con.port, dragPort };
							}

							graph.addLink(li);
						}
					}
				}
			}

			if (drop)
			{
				stopDragging();
				return;
			}

			break;
		}
		}
	}

	void drawNodes(nodegraph::Graph& graph, INodeGraphGuiGlue& glue, ImDrawList* const drawList, const ImVec2& offset)
	{
		const ImVec2 NODE_WINDOW_PADDING(12.0f, 8.0f);

		if (ImGui::IsMouseClicked(0)) {
			nodeSelected = nodegraph::node_handle();
		}

		// Display nodes
		graph.iterNodes([&](nodegraph::node_handle nodeHandle)
		{
			NodeState& node = nodes[nodeHandle.idx];
			ImGui::PushID(nodeHandle.idx);
			ImVec2 node_rect_min = offset + node.Pos;

			// Display node contents first
			drawList->ChannelsSetCurrent(2); // Foreground
			bool old_any_active = ImGui::IsAnyItemActive();
			ImGui::SetCursorScreenPos(node_rect_min + NODE_WINDOW_PADDING);

			ImGui::BeginGroup();
			ImGui::Text(glue.getNodeName(nodeHandle).c_str());
			ImGui::Dummy(ImVec2(0, 5));

			const float nodeHeaderMaxY = ImGui::GetCursorScreenPos().y;

			ImGui::BeginGroup();

			ImGui::BeginGroup(); // Lock horizontal position
			graph.iterNodeInputPorts(nodeHandle, [&](nodegraph::port_handle portHandle)
			{
				auto portInfo = glue.getPortInfo(portHandle);
				ImVec2 cursorLeft = ImGui::GetCursorScreenPos();
				ImColor textColor = defaultPortLabelColor;
				if (!portInfo.valid) textColor = invalidPortLabelColor;

				ImGui::PushStyleColor(ImGuiCol_Text, textColor);
				ImGui::Text(portInfo.name.c_str());
				ImGui::PopStyleColor();

				ports[portHandle.idx].pos = cursorLeft + ImVec2(-NODE_WINDOW_PADDING.x, 0.5f * ImGui::GetItemRectSize().y);
				ports[portHandle.idx].valid = portInfo.valid;
			});
			ImGui::EndGroup();

			// Make some space in the middle
			ImGui::SameLine();
			ImGui::Dummy(ImVec2(20, 0));

			ImGui::SameLine();

			ImGui::BeginGroup(); // Lock horizontal position
			float cursorStart = ImGui::GetCursorPosX();
			float maxWidth = 0.0f;

			graph.iterNodeOutputPorts(nodeHandle, [&](nodegraph::port_handle portHandle) {
				maxWidth = std::max(maxWidth, ImGui::CalcTextSize(glue.getPortInfo(portHandle).name.c_str()).x);
			});

			graph.iterNodeOutputPorts(nodeHandle, [&](nodegraph::port_handle portHandle)
			{
				auto portInfo = glue.getPortInfo(portHandle);
				const std::string name = portInfo.name;
				const float width = ImGui::CalcTextSize(name.c_str()).x;
				ImGui::SetCursorPosX(cursorStart + maxWidth - width);
				ImVec2 cursorLeft = ImGui::GetCursorScreenPos();

				ImColor textColor = defaultPortLabelColor;
				if (!portInfo.valid) textColor = invalidPortLabelColor;

				ImGui::PushStyleColor(ImGuiCol_Text, textColor);
				ImGui::Text(name.c_str());
				ImGui::PopStyleColor();

				ports[portHandle.idx].pos = cursorLeft + ImVec2(NODE_WINDOW_PADDING.x + width, 0.5f * ImGui::GetItemRectSize().y);
				ports[portHandle.idx].valid = portInfo.valid;
			});
			ImGui::EndGroup();

			ImGui::EndGroup();
			ImGui::EndGroup();

			// Note: Could draw node interior here

			ImGui::GetCursorPos();

			// Save the size of what we have emitted and whether any of the widgets are being used
			bool node_widgets_active = (!old_any_active && ImGui::IsAnyItemActive());
			node.Size = ImGui::GetItemRectSize() + NODE_WINDOW_PADDING + NODE_WINDOW_PADDING;
			ImVec2 node_rect_max = node_rect_min + node.Size;

			// Display node box
			drawList->ChannelsSetCurrent(0); // Background
			ImGui::SetCursorScreenPos(node_rect_min);
			ImGui::InvisibleButton("node", node.Size);
			if (ImGui::IsItemHovered())
			{
				nodeHoveredInScene = nodeHandle;
				openContextMenu |= ImGui::IsMouseClicked(1);

				if (ImGui::IsMouseDoubleClicked(0)) {
					glue.onTriggered(nodeHandle);
				}
			}
			bool node_moving_active = ImGui::IsItemActive();
			if (node_widgets_active || node_moving_active)
				nodeSelected = nodeHandle;
			if (node_moving_active && ImGui::IsMouseDragging(0) && s_dragState == DragState_Default)
				node.Pos = node.Pos + ImGui::GetIO().MouseDelta;

			ImU32 node_bg_color = (nodeHoveredInScene == nodeHandle || nodeSelected == nodeHandle) ? ImColor(75, 75, 75) : ImColor(60, 60, 60);
			drawList->AddRectFilled(node_rect_min, node_rect_max, node_bg_color, 8.0f);
			drawList->AddRectFilled(node_rect_min, ImVec2(node_rect_max.x, nodeHeaderMaxY - 6), ImColor(255, 255, 255, 32), 8.0f, 1 | 2);

			ImColor frameColor = ImColor(255, 255, 255, 20);
			drawList->AddLine(ImVec2(node_rect_min.x, nodeHeaderMaxY - 6 - 1), ImVec2(node_rect_max.x, nodeHeaderMaxY - 6 - 1), frameColor);
			drawList->AddRect(node_rect_min, node_rect_max, frameColor, 8.0f);

			drawList->ChannelsSetCurrent(2); // Foreground

			graph.iterNodeInputPorts(nodeHandle, [&](nodegraph::port_handle portHandle)
			{
				drawNodeConnector(drawList, ports[portHandle.idx].pos, ports[portHandle.idx].valid ? defaultPortColor : invalidPortColor);
			});

			graph.iterNodeOutputPorts(nodeHandle, [&](nodegraph::port_handle portHandle)
			{
				drawNodeConnector(drawList, ports[portHandle.idx].pos, ports[portHandle.idx].valid ? defaultPortColor : invalidPortColor);
			});

			ImGui::PopID();
		});
	}

	void drawLinks(nodegraph::Graph& graph, INodeGraphGuiGlue& glue, ImDrawList* const drawList, const ImVec2& offset)
	{
		// Display links
		drawList->ChannelsSetCurrent(1); // Background

		graph.iterNodes([&](nodegraph::node_handle nodeHandle)
		{
			graph.iterNodeInputPorts(nodeHandle, [&](nodegraph::port_handle portHandle)
			{
				const nodegraph::Port& port = graph.ports[portHandle.idx];
				if (port.link != nodegraph::invalid_link_idx) {
					const nodegraph::Link& link = graph.links[port.link];

					ImVec2 p1 = getPortPos(link.srcPort);
					ImVec2 p2 = getPortPos(link.dstPort);

					const auto p1info = glue.getPortInfo(graph.portHandle(link.srcPort));
					const auto p2info = glue.getPortInfo(graph.portHandle(link.dstPort));

					BezierCurve curve = getNodeLinkCurve(p1, p2);
					float f = curve.distanceToPoint(ImGui::GetMousePos());

					ImColor linkColor = ImColor(200, 200, 100, 128);

					/*if (f < 10.f) {
						drawNodeLink(drawList, curve, ImColor(200, 200, 100, 255));

						// TODO: selection of links.

						const ImGuiIO& io = ImGui::GetIO();
						if (ImGui::IsKeyReleased(io.KeyMap[ImGuiKey_Delete])) {
							//link.requestDelete = true;
							// TODO
						}
					}
					else */{
						if (!p1info.valid || !p2info.valid) {
							linkColor = ImColor(255, 32, 8, 255);
						}
						drawNodeLink(drawList, curve, linkColor);
					}
				}
			});
		});
	}

	void drawGrid(ImDrawList* const drawList, const ImVec2& offset)
	{
		ImU32 GRID_COLOR = ImColor(255, 255, 255, 10);
		float GRID_SZ = 32.0f;
		ImVec2 win_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_sz = ImGui::GetWindowSize();
		for (float x = fmodf(offset.x, GRID_SZ); x < canvas_sz.x; x += GRID_SZ)
			drawList->AddLine(ImVec2(x, 0.0f) + win_pos, ImVec2(x, canvas_sz.y) + win_pos, GRID_COLOR);
		for (float y = fmodf(offset.y, GRID_SZ); y < canvas_sz.y; y += GRID_SZ)
			drawList->AddLine(ImVec2(0.0f, y) + win_pos, ImVec2(canvas_sz.x, y) + win_pos, GRID_COLOR);
	}

	void doGui(nodegraph::Graph& graph, INodeGraphGuiGlue& glue)
	{
		openContextMenu = false;
		nodeHoveredInScene = nodegraph::node_handle();

		nodes.resize(graph.nodes.size());
		ports.resize(graph.ports.size());

		// TODO: spawning of multiple nodes with offsets
		graph.iterNodes([&](nodegraph::node_handle nodeHandle)
		{
			if (nodes[nodeHandle.idx].Size.x == 0) {
				const ImVec2 mousePos = ImGui::GetIO().MousePos + scrolling - this->originOffset;
				nodes[nodeHandle.idx].Pos = mousePos;
			}
		});


		ImGui::BeginGroup();
		ImGui::PushItemWidth(120.0f);

		this->originOffset = ImGui::GetCursorScreenPos();
		ImVec2 offset = this->originOffset - scrolling;
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		drawList->ChannelsSplit(3);
		{
			drawGrid(drawList, offset);
			drawNodes(graph, glue, drawList, offset);
			updateDragging(graph, glue, drawList, scrolling);
			drawLinks(graph, glue, drawList, offset);
		}
		drawList->ChannelsMerge();

		// Open context menu
		if (!ImGui::IsAnyItemHovered() && ImGui::IsMouseHoveringWindow() && ImGui::IsMouseClicked(1))
		{
			nodeSelected = nodeHoveredInScene = nodegraph::node_handle();
			openContextMenu = true;
		}
		if (openContextMenu)
		{
			ImGui::OpenPopup("context_menu");
			if (nodeHoveredInScene.valid())
				nodeSelected = nodeHoveredInScene;
		}

		// TODO: exclusive selection among nodes and links

		const ImGuiIO& io = ImGui::GetIO();
		if (nodeSelected.valid() && ImGui::IsKeyReleased(io.KeyMap[ImGuiKey_Delete])) {
			glue.onNodeRemoved(nodeSelected);
			graph.removeNode(nodeSelected);
			nodeSelected = nodegraph::node_handle();
		}

		// Draw context menu
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
		if (ImGui::BeginPopup("context_menu"))
		{
			glue.onContextMenu(/*nodeSelected*/);
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();

		// Scrolling
		if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() && ImGui::IsMouseDragging(2, 0.0f))
			scrolling = scrolling - ImGui::GetIO().MouseDelta;

		ImGui::EndGroup();
	}
};

static std::unordered_map<ImGuiID, NodeGraphState> g_nodeGraphs;

void nodeGraph(nodegraph::Graph& graph, INodeGraphGuiGlue& glue)
{
	const ImGuiID id = ImGui::GetID(&graph);
	g_nodeGraphs[id].doGui(graph, glue);
}

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

FixedString& FixedString::operator=(const char* const n) {
	strncpy(data, n, sizeof(data) - 1);
	data[sizeof(data) - 1] = '\0';
	return *this;
}

FixedString& FixedString::operator=(const std::string& n) {
	size_t maxLen = std::min(sizeof(data) - 1, n.size());
	strncpy(data, n.data(), maxLen);
	data[maxLen] = '\0';
	return *this;
}

struct NodeImpl
{
	ImVec2 Pos = { 0, 0 }, Size = { 0, 0 };
	std::vector<ImVec2> inputSlotPos;
	std::vector<ImVec2> outputSlotPos;
};

struct Connector {
	NodeInfo* node;
	size_t port;
	bool isOutput;

	static Connector invalid() {
		Connector res;
		res.node = nullptr;
		res.port = 0;
		res.isOutput = false;
		return res;
	}

	ImVec2 pos() const {
		return isOutput ? node->impl->outputSlotPos[port] : node->impl->inputSlotPos[port];
	}

	operator bool() const {
		return !!node;
	}

	bool operator==(const Connector& o) const {
		return node == o.node && port == o.port && isOutput == o.isOutput;
	}
	bool operator!=(const Connector& o) const {
		return !(*this == o);
	}
};

enum DragState {
	DragState_Default,
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

static Connector s_dragNode;
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

struct LinkState : LinkInfo {
	BezierCurve curve;
	bool mark : 1;
	bool requestDelete : 1;

	LinkState()
		: mark(false)
		, requestDelete(false)
	{}
};

struct NodeGraphState
{
	std::vector<NodeInfo*> nodes;
	std::vector<LinkState> links;

	ImVec2 scrolling = ImVec2(0.0f, 0.0f);
	ImVec2 originOffset = ImVec2(0.0f, 0.0f);
	NodeInfo* nodeSelected = nullptr;

	bool openContextMenu = false;
	NodeInfo* nodeHoveredInScene = nullptr;

	Connector getHoverCon(ImVec2 offset, float maxDist)
	{
		const ImVec2 mousePos = ImGui::GetIO().MousePos;

		for (NodeInfo* node : nodes)
		{
			{
				float closestDist = 1e5f;
				size_t closest = 0;

				for (size_t i = 0; i < node->inputs.size(); ++i)
				{
					const float d = distance(node->impl->inputSlotPos[i], mousePos);
					if (d < closestDist) {
						closestDist = d;
						closest = i;
					}
				}

				if (closestDist < maxDist) {
					return Connector{ node, closest, false };
				}
			}

			{
				float closestDist = 1e5f;
				size_t closest = 0;

				for (size_t i = 0; i < node->outputs.size(); ++i)
				{
					//printf("o: %f %f\n", node->impl->outputSlotPos[i].x, node->impl->outputSlotPos[i].y);
					const float d = distance(node->impl->outputSlotPos[i], mousePos);
					if (d < closestDist) {
						closestDist = d;
						closest = i;
					}
				}

				if (closestDist < maxDist) {
					return Connector{ node, closest, true };
				}
			}
		}

		return Connector::invalid();
	}

	const float NODE_SLOT_RADIUS = 5.0f;

	void drawNodeConnector(ImDrawList* const draw_list, const ImVec2& pos, ImColor col = ImColor(150, 150, 150, 255))
	{
		draw_list->AddCircleFilled(pos, NODE_SLOT_RADIUS, col, 12);
	}

	void stopDragging()
	{
		s_dragNode = Connector::invalid();
		s_dragState = DragState_Default;
	}

	// Must be called after drawNodes
	void updateDragging(INodeGraphBackend *const backend, ImDrawList* const draw_list, ImVec2 offset)
	{
		switch (s_dragState)
		{
		case DragState_Default:
		{
			if (Connector con = getHoverCon(offset, NODE_SLOT_RADIUS * 1.5f)) {
				if (ImGui::IsMouseClicked(0)) {
					s_dragNode = con;
					s_dragState = DragState_Dragging;
				}
			}
			break;
		}

		case DragState_Dragging:
		{
			BezierCurve linkCurve;

			if (s_dragNode.isOutput) {
				linkCurve = getNodeLinkCurve(s_dragNode.pos(), ImGui::GetIO().MousePos);
			} else {
				linkCurve = getNodeLinkCurve(ImGui::GetIO().MousePos, s_dragNode.pos());
			}

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->ChannelsSetCurrent(1);
			drawNodeLink(drawList, linkCurve);

			const bool drop = !ImGui::IsMouseDown(0);

			Connector con = getHoverCon(offset, NODE_SLOT_RADIUS * 3.f);
			if (con && con.node != s_dragNode.node && con.isOutput != s_dragNode.isOutput)
			{
				LinkInfo li;
				if (s_dragNode.isOutput) {
					li = { s_dragNode.node, con.node, s_dragNode.port, con.port };
				}
				else {
					li = { con.node, s_dragNode.node, con.port, s_dragNode.port };
				}

				drawList->ChannelsSetCurrent(2);
				drawNodeConnector(draw_list, con.pos(), ImColor(32, 220, 120, 255));

				if (drop)
				{
					// Lets connect the nodes.
					if (backend->onConnected(li))
					{
						LinkState ls;
						static_cast<LinkInfo&>(ls) = li;
						links.push_back(ls);
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

	FreeList<NodeImpl>& nodeImplPool() {
		FreeList<NodeImpl> inst;
		return inst;
	}

	void updateNodes(INodeGraphBackend *const backend)
	{
		static FreeList<NodeImpl> nodeImplPool;
		bool selectedNodeFound = false;
		bool dragNodeFound = false;

		nodes.clear();
		nodes.resize(backend->getNodeCount());
		for (size_t i = 0; i < nodes.size(); ++i) {
			nodes[i] = backend->getNodeByIdx(i);
			if (!nodes[i]->impl) {
				const ImVec2 mousePos = ImGui::GetIO().MousePos + scrolling - this->originOffset;

				nodes[i]->impl = nodeImplPool.alloc();
				nodes[i]->impl->Pos = mousePos;
			} else {
				if (nodes[i] == nodeSelected) {
					selectedNodeFound = true;
				}
				if (nodes[i] == s_dragNode.node) {
					dragNodeFound = true;
				}
			}
		}

		if (!selectedNodeFound) {
			nodeSelected = nullptr;
		}
		if (!dragNodeFound && s_dragNode.node) {
			stopDragging();
		}
	}

	void updateLinks()
	{
		//auto firstToRemove = std::partition(links.begin(), links.end(), [](const LinkState& s) { return !s.requestDelete; });
		//links.erase(firstToRemove, links.end());

		links.erase(
			std::remove_if(links.begin(), links.end(), [](const LinkState& s) { return s.requestDelete; }),
			links.end()
		);
	}

	void drawGrid(ImDrawList* const draw_list, const ImVec2& offset)
	{
		ImU32 GRID_COLOR = ImColor(255, 255, 255, 10);
		float GRID_SZ = 32.0f;
		ImVec2 win_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_sz = ImGui::GetWindowSize();
		for (float x = fmodf(offset.x, GRID_SZ); x < canvas_sz.x; x += GRID_SZ)
			draw_list->AddLine(ImVec2(x, 0.0f) + win_pos, ImVec2(x, canvas_sz.y) + win_pos, GRID_COLOR);
		for (float y = fmodf(offset.y, GRID_SZ); y < canvas_sz.y; y += GRID_SZ)
			draw_list->AddLine(ImVec2(0.0f, y) + win_pos, ImVec2(canvas_sz.x, y) + win_pos, GRID_COLOR);
	}

	void drawNodes(INodeGraphBackend *const backend, ImDrawList* const draw_list, const ImVec2& offset)
	{
		const ImVec2 NODE_WINDOW_PADDING(12.0f, 8.0f);

		if (ImGui::IsMouseClicked(0)) {
			nodeSelected = nullptr;
		}

		// Display nodes
		for (int node_idx = 0; node_idx < nodes.size(); node_idx++)
		{
			NodeInfo* node = nodes[node_idx];
			ImGui::PushID(node);
			ImVec2 node_rect_min = offset + node->impl->Pos;

			// Display node contents first
			draw_list->ChannelsSetCurrent(2); // Foreground
			bool old_any_active = ImGui::IsAnyItemActive();
			ImGui::SetCursorScreenPos(node_rect_min + NODE_WINDOW_PADDING);

			ImGui::BeginGroup();
			ImGui::Text(node->name.data);
			ImGui::Dummy(ImVec2(0, 5));

			const float nodeHeaderMaxY = ImGui::GetCursorScreenPos().y;

			ImGui::BeginGroup();

			ImGui::BeginGroup(); // Lock horizontal position
			node->impl->inputSlotPos.clear();
			for (const auto& x : node->inputs) {
				ImVec2 cursorLeft = ImGui::GetCursorScreenPos();
				ImGui::Text(x.name.data);
				node->impl->inputSlotPos.push_back(cursorLeft + ImVec2(-NODE_WINDOW_PADDING.x, 0.5f * ImGui::GetItemRectSize().y));
			}
			ImGui::EndGroup();

			// Make some space in the middle
			ImGui::SameLine();
			ImGui::Dummy(ImVec2(20, 0));

			ImGui::SameLine();

			ImGui::BeginGroup(); // Lock horizontal position
			float cursorStart = ImGui::GetCursorPosX();
			float maxWidth = 0.0f;
			node->impl->outputSlotPos.clear();
			for (const auto& x : node->outputs) {
				maxWidth = std::max(maxWidth, ImGui::CalcTextSize(x.name.data).x);
			}

			for (const auto& x : node->outputs) {
				const float width = ImGui::CalcTextSize(x.name.data).x;
				ImGui::SetCursorPosX(cursorStart + maxWidth - width);
				ImVec2 cursorLeft = ImGui::GetCursorScreenPos();
				ImGui::Text(x.name.data);
				node->impl->outputSlotPos.push_back(cursorLeft + ImVec2(NODE_WINDOW_PADDING.x + width, 0.5f * ImGui::GetItemRectSize().y));
			}
			ImGui::EndGroup();

			ImGui::EndGroup();
			ImGui::EndGroup();

			// Note: Could draw node interior here

			ImGui::GetCursorPos();

			// Save the size of what we have emitted and whether any of the widgets are being used
			bool node_widgets_active = (!old_any_active && ImGui::IsAnyItemActive());
			node->impl->Size = ImGui::GetItemRectSize() + NODE_WINDOW_PADDING + NODE_WINDOW_PADDING;
			ImVec2 node_rect_max = node_rect_min + node->impl->Size;

			// Display node box
			draw_list->ChannelsSetCurrent(0); // Background
			ImGui::SetCursorScreenPos(node_rect_min);
			ImGui::InvisibleButton("node", node->impl->Size);
			if (ImGui::IsItemHovered())
			{
				nodeHoveredInScene = node;
				openContextMenu |= ImGui::IsMouseClicked(1);

				if (ImGui::IsMouseDoubleClicked(0)) {
					backend->onTriggered(node);
				}
			}
			bool node_moving_active = ImGui::IsItemActive();
			if (node_widgets_active || node_moving_active)
				nodeSelected = node;
			if (node_moving_active && ImGui::IsMouseDragging(0) && s_dragState == DragState_Default)
				node->impl->Pos = node->impl->Pos + ImGui::GetIO().MouseDelta;

			ImU32 node_bg_color = (nodeHoveredInScene == node || nodeSelected == node) ? ImColor(75, 75, 75) : ImColor(60, 60, 60);
			draw_list->AddRectFilled(node_rect_min, node_rect_max, node_bg_color, 8.0f);
			draw_list->AddRectFilled(node_rect_min, ImVec2(node_rect_max.x, nodeHeaderMaxY - 6), ImColor(255, 255, 255, 32), 8.0f, 1 | 2);

			ImColor frameColor = ImColor(255, 255, 255, 20);
			draw_list->AddLine(ImVec2(node_rect_min.x, nodeHeaderMaxY - 6 - 1), ImVec2(node_rect_max.x, nodeHeaderMaxY - 6 - 1), frameColor);
			draw_list->AddRect(node_rect_min, node_rect_max, frameColor, 8.0f);

			draw_list->ChannelsSetCurrent(2); // Foreground

			for (const ImVec2& pos : node->impl->inputSlotPos) {
				drawNodeConnector(draw_list, pos);
			}
			for (const ImVec2& pos : node->impl->outputSlotPos) {
				drawNodeConnector(draw_list, pos);
			}

			ImGui::PopID();
		}
	}

	void drawLinks(ImDrawList* const draw_list, const ImVec2& offset)
	{
		// Display links
		draw_list->ChannelsSetCurrent(1); // Background

		for (LinkState& link : links)
		{
			const NodeImpl* srcNode = link.srcNode->impl;
			const NodeImpl* dstNode = link.dstNode->impl;
			const ImVec2& p1 = srcNode->outputSlotPos[link.srcPort];
			const ImVec2& p2 = dstNode->inputSlotPos[link.dstPort];

			BezierCurve curve = getNodeLinkCurve(p1, p2);
			float f = curve.distanceToPoint(ImGui::GetMousePos());

			if (f < 10.f) {
				drawNodeLink(draw_list, curve, ImColor(200, 200, 100, 255));

				const ImGuiIO& io = ImGui::GetIO();
				if (ImGui::IsKeyReleased(io.KeyMap[ImGuiKey_Delete])) {
					link.requestDelete = true;
				}
			} else {
				drawNodeLink(draw_list, curve);
			}
		}
	}

	void doGui(INodeGraphBackend *const backend)
	{
		openContextMenu = false;
		nodeHoveredInScene = nullptr;

		updateNodes(backend);
		updateLinks();

		ImGui::BeginGroup();
		ImGui::PushItemWidth(120.0f);

		this->originOffset = ImGui::GetCursorScreenPos();
		ImVec2 offset = this->originOffset - scrolling;
		ImDrawList* draw_list = ImGui::GetWindowDrawList();

		draw_list->ChannelsSplit(3);
		{
			drawGrid(draw_list, offset);
			drawNodes(backend, draw_list, offset);
			updateDragging(backend, draw_list, scrolling);
			drawLinks(draw_list, offset);
		}
		draw_list->ChannelsMerge();

		// Open context menu
		if (!ImGui::IsAnyItemHovered() && ImGui::IsMouseHoveringWindow() && ImGui::IsMouseClicked(1))
		{
			nodeSelected = nodeHoveredInScene = nullptr;
			openContextMenu = true;
		}
		if (openContextMenu)
		{
			ImGui::OpenPopup("context_menu");
			if (nodeHoveredInScene != nullptr)
				nodeSelected = nodeHoveredInScene;
		}

		// Draw context menu
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
		if (ImGui::BeginPopup("context_menu"))
		{
			backend->onContextMenu(nodeSelected);
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

// Really dumb data structure provided for the example.
// Note that we storing links are INDICES (not ID) to make example code shorter, obviously a bad idea for any general purpose code.
void nodeGraph(INodeGraphBackend *const backend)
{
	const ImGuiID id = ImGui::GetID(backend);
	g_nodeGraphs[id].doGui(backend);
}
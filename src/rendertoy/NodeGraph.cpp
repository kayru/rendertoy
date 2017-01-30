// ImGui - standalone example application for Glfw + OpenGL 3, using programmable pipeline

#include "NodeGraph.h"
#include <imgui.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <algorithm>
#include <unordered_map>
#include <stdint.h>

// NB: You can use math functions/operators on ImVec2 if you #define IMGUI_DEFINE_MATH_OPERATORS and #include "imgui_internal.h"
// Here we only declare simple +/- operators so others don't leak into the demo code.
static inline ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x+rhs.x, lhs.y+rhs.y); }
static inline ImVec2 operator-(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x-rhs.x, lhs.y-rhs.y); }

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

void drawHermite(ImDrawList* drawList, ImVec2 p1, ImVec2 p2, int STEPS)
{
	ImVec2 t1 = ImVec2(+80.0f, 0.0f);
	ImVec2 t2 = ImVec2(+80.0f, 0.0f);

	for (int step = 0; step <= STEPS; step++)
	{
		float t = (float)step / (float)STEPS;
		float h1 = +2 * t*t*t - 3 * t*t + 1.0f;
		float h2 = -2 * t*t*t + 3 * t*t;
		float h3 = t*t*t - 2 * t*t + t;
		float h4 = t*t*t - t*t;
		drawList->PathLineTo(ImVec2(h1*p1.x + h2*p2.x + h3*t1.x + h4*t2.x, h1*p1.y + h2*p2.y + h3*t1.y + h4*t2.y));
	}

	drawList->PathStroke(ImColor(200, 200, 100), false, 3.0f);
}


static size_t uid(const NodeInfo* ni) {
	return reinterpret_cast<size_t>(ni);
}

struct Connector {
	NodeInfo* node;
	size_t port;

	static Connector invalid() {
		Connector res;
		res.node = nullptr;
		res.port = 0;
		return res;
	}

	operator bool() const {
		return !!node;
	}

	bool operator==(const Connector& o) const {
		return node == o.node && port == o.port;
	}
	bool operator!=(const Connector& o) const {
		return !(*this == o);
	}
};

struct DragNode
{
	ImVec2 pos;
	Connector con;
};

enum DragState
{
	DragState_Default,
	DragState_Hover,
	DragState_BeginDrag,
	DragState_Draging,
	DragState_Connect,
};

static DragNode s_dragNode;
static DragState s_dragState = DragState_Default;

struct NodeImpl
{
	ImVec2 Pos = { 0, 0 }, Size = { 0, 0 };
	std::vector<ImVec2> inputSlotPos;
	std::vector<ImVec2> outputSlotPos;
};

/*struct NodeLink
{
	NodeInfo* srcNode;
	NodeInfo* dstNode;
	size_t srcPort;
	size_t dstPort;
};*/

struct NodeGraphState
{
	std::vector<NodeInfo*> nodes;
	std::list<NodeImpl> nodeImpls;	// TODO: change to a flat vector with gc
	std::vector<LinkInfo> links;

	ImVec2 scrolling = ImVec2(0.0f, 0.0f);
	int node_selected = -1;

	bool open_context_menu = false;
	int node_hovered_in_scene = -1;


	/*static bool isConnectorHovered(NodeLink* c, ImVec2 offset)
	{
		ImVec2 mousePos = ImGui::GetIO().MousePos;
		ImVec2 conPos = offset + c->pos;

		float xd = mousePos.x - conPos.x;
		float yd = mousePos.y - conPos.y;

		return ((xd * xd) + (yd *yd)) < (NODE_SLOT_RADIUS * NODE_SLOT_RADIUS);
	}*/

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static float dist(const ImVec2& a, const ImVec2& b) {
		ImVec2 c = a - b;
		return sqrtf(c.x * c.x + c.y * c.y);
	}

	Connector getHoverCon(ImVec2 offset, ImVec2* pos)
	{
		const ImVec2 mousePos = ImGui::GetIO().MousePos;
		//printf("mousePos: %f %f\n", mousePos.x, mousePos.y);

		for (NodeInfo* node : nodes)
		{
			{
				float closestDist = 1e5f;
				size_t closest = 0;

				for (size_t i = 0; i < node->inputs.size(); ++i)
				{
					const float d = dist(node->impl->inputSlotPos[i], mousePos);
					if (d < closestDist) {
						closestDist = d;
						closest = i;
					}
				}

				if (closestDist < 8.0f) {
					*pos = node->impl->inputSlotPos[closest];
					return Connector{ node, closest };
				}
			}

			{
				float closestDist = 1e5f;
				size_t closest = 0;

				for (size_t i = 0; i < node->outputs.size(); ++i)
				{
					//printf("o: %f %f\n", node->impl->outputSlotPos[i].x, node->impl->outputSlotPos[i].y);
					const float d = dist(node->impl->outputSlotPos[i], mousePos);
					if (d < closestDist) {
						closestDist = d;
						closest = i;
					}
				}

				if (closestDist < 8.0f) {
					*pos = node->impl->outputSlotPos[closest];
					return Connector{ node, closest };
				}
			}
		}

		return Connector::invalid();
	}

	void updateDraging(INodeGraphBackend *const backend, ImVec2 offset)
	{
		switch (s_dragState)
		{
		case DragState_Default:
		{
			ImVec2 pos;
			if (Connector con = getHoverCon(offset, &pos)) {
				s_dragNode.con = con;
				s_dragNode.pos = pos;
				s_dragState = DragState_Hover;
				return;
			}

			break;
		}

		case DragState_Hover:
		{
			ImVec2 pos;
			Connector con = getHoverCon(offset, &pos);

			// Make sure we are still hovering the same node

			if (con != s_dragNode.con)
			{
				s_dragNode.con = Connector::invalid();
				s_dragState = DragState_Default;
				return;
			}

			if (ImGui::IsMouseClicked(0) && s_dragNode.con)
				s_dragState = DragState_Draging;

			break;
		}

		case DragState_BeginDrag:
		{
			break;
		}

		case DragState_Draging:
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();

			drawList->ChannelsSetCurrent(1);

			drawHermite(drawList, s_dragNode.pos, ImGui::GetIO().MousePos, 12);

			if (!ImGui::IsMouseDown(0))
			{
				ImVec2 pos;
				Connector con = getHoverCon(offset, &pos);

				// Make sure we are still hovering the same node

				if (!con || con.node == s_dragNode.con.node)
				{
					s_dragNode.con = Connector::invalid();
					s_dragState = DragState_Default;
					return;
				}

				// Lets connect the nodes.
				// TODO: Make sure we connect stuff in the correct way!

				LinkInfo li = { s_dragNode.con.node, con.node, s_dragNode.con.port, con.port };
				if (backend->onConnected(li))
				{
					links.push_back(li);
				}

				// TODO
				//con->input = s_dragNode.con;

				s_dragNode.con = Connector::invalid();
				s_dragState = DragState_Default;
			}

			break;
		}

		case DragState_Connect:
		{
			break;
		}
		}
	}

	void updateNodes(INodeGraphBackend *const backend)
	{
		nodes.clear();
		nodes.resize(backend->getNodeCount());
		for (size_t i = 0; i < nodes.size(); ++i) {
			nodes[i] = backend->getNodeByIdx(i);
			if (!nodes[i]->impl) {
				nodeImpls.push_back(NodeImpl());
				nodes[i]->impl = &nodeImpls.back();
				ImVec2 mousePos = ImGui::GetIO().MousePos;
				if (mousePos.x > -9000) {
					nodes[i]->impl->Pos = mousePos;
				} else {
					nodes[i]->impl->Pos = ImVec2(i % 5 * 200 + 20, 200);
				}
			}
		}

		// HACK
		//links.clear();
		//links.push_back({ nodes[0], nodes[1], 2, 0 });
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
		const float NODE_SLOT_RADIUS = 5.0f;
		const ImVec2 NODE_WINDOW_PADDING(12.0f, 8.0f);

		// Display nodes
		for (int node_idx = 0; node_idx < nodes.size(); node_idx++)
		{
			NodeInfo* node = nodes[node_idx];
			ImGui::PushID(uid(node));
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

			/*ImGui::Text("%s", node->Name);
			ImGui::SliderFloat("##value", &node->Value, 0.0f, 1.0f, "Alpha %.2f");
			ImGui::ColorEdit3("##color", &node->Color.x);*/
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
				node_hovered_in_scene = uid(node);
				open_context_menu |= ImGui::IsMouseClicked(1);

				if (ImGui::IsMouseDoubleClicked(0)) {
					backend->onTriggered(node);
				}
			}
			bool node_moving_active = ImGui::IsItemActive();
			if (node_widgets_active || node_moving_active)
				node_selected = uid(node);
			if (node_moving_active && ImGui::IsMouseDragging(0) && s_dragState == DragState_Default)
				node->impl->Pos = node->impl->Pos + ImGui::GetIO().MouseDelta;

			ImU32 node_bg_color = (node_hovered_in_scene == uid(node) || node_selected == uid(node)) ? ImColor(75, 75, 75) : ImColor(60, 60, 60);
			draw_list->AddRectFilled(node_rect_min, node_rect_max, node_bg_color, 8.0f);
			draw_list->AddRectFilled(node_rect_min, ImVec2(node_rect_max.x, nodeHeaderMaxY - 6), ImColor(255, 255, 255, 32), 8.0f, 1 | 2);

			ImColor frameColor = ImColor(255, 255, 255, 20);
			draw_list->AddLine(ImVec2(node_rect_min.x, nodeHeaderMaxY - 6 - 1), ImVec2(node_rect_max.x, nodeHeaderMaxY - 6 - 1), frameColor);
			draw_list->AddRect(node_rect_min, node_rect_max, frameColor, 8.0f);

			draw_list->ChannelsSetCurrent(2); // Foreground

			for (const ImVec2& pos : node->impl->inputSlotPos) {
				draw_list->AddCircleFilled(pos, NODE_SLOT_RADIUS, ImColor(150, 150, 150, 255), 12);
				//draw_list->AddCircle(pos, NODE_SLOT_RADIUS, ImColor(0, 0, 0, 128), 12, 1.0f);
			}
			for (const ImVec2& pos : node->impl->outputSlotPos) {
				draw_list->AddCircleFilled(pos, NODE_SLOT_RADIUS, ImColor(150, 150, 150, 255), 12);
			}

			ImGui::PopID();
		}
	}

	void drawLinks(ImDrawList* const draw_list, const ImVec2& offset)
	{
		// Display links
		draw_list->ChannelsSetCurrent(1); // Background
		for (int link_idx = 0; link_idx < links.size(); link_idx++)
		{
			LinkInfo* link = &links[link_idx];
			NodeImpl* node_inp = link->srcNode->impl;
			NodeImpl* node_out = link->dstNode->impl;
			ImVec2 p1 = node_inp->outputSlotPos[link->srcPort];
			ImVec2 p2 = node_out->inputSlotPos[link->dstPort];
			draw_list->AddBezierCurve(p1, p1 + ImVec2(+50, 0), p2 + ImVec2(-50, 0), p2, ImColor(200, 200, 100), 3.0f);
		}
	}

	void doGui(INodeGraphBackend *const backend)
	{
		open_context_menu = false;
		node_hovered_in_scene = -1;

		updateNodes(backend);

		ImGui::BeginGroup();

		// Create our child canvas
		//ImGui::Text("Hold middle mouse button to scroll (%.2f,%.2f)", scrolling.x, scrolling.y);
		//ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1, 1));
		/*ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::PushStyleColor(ImGuiCol_ChildWindowBg, ImColor(44, 44, 44, 200));
		ImGui::PushStyleColor(ImGuiCol_Border, ImColor(255, 255, 255, 32));
		const bool scollingRegionBorder = false;
		ImGui::BeginChild("scrolling_region", ImVec2(0, 0), scollingRegionBorder, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);*/
		ImGui::PushItemWidth(120.0f);

		ImVec2 offset = ImGui::GetCursorScreenPos() - scrolling;
		ImDrawList* draw_list = ImGui::GetWindowDrawList();

		draw_list->ChannelsSplit(3);
		{
			drawGrid(draw_list, offset);
			drawNodes(backend, draw_list, offset);
			updateDraging(backend, scrolling);
			drawLinks(draw_list, offset);
		}
		draw_list->ChannelsMerge();

		// Open context menu
		if (!ImGui::IsAnyItemHovered() && ImGui::IsMouseHoveringWindow() && ImGui::IsMouseClicked(1))
		{
			node_selected = node_hovered_in_scene = -1;
			open_context_menu = true;
		}
		if (open_context_menu)
		{
			ImGui::OpenPopup("context_menu");
			if (node_hovered_in_scene != -1)
				node_selected = node_hovered_in_scene;
		}

		// Draw context menu
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
		if (ImGui::BeginPopup("context_menu"))
		{
			NodeInfo* node = node_selected != -1 ? nodes[node_selected] : NULL;
			ImVec2 scene_pos = ImGui::GetMousePosOnOpeningCurrentPopup() - offset;
			if (node)
			{
				ImGui::Text("Node '%s'", uid(node));
				ImGui::Separator();
				if (ImGui::MenuItem("Rename..", NULL, false, false)) {}
				if (ImGui::MenuItem("Delete", NULL, false, false)) {}
				if (ImGui::MenuItem("Copy", NULL, false, false)) {}
			}
			else
			{
				//if (ImGui::MenuItem("Add")) { nodes.push_back(Node(nodes.Size, "New node", scene_pos, 0.5f, ImColor(100,100,200), 2, 2)); }
				//if (ImGui::MenuItem("Paste", NULL, false, false)) {}
				std::vector<std::string> items;
				backend->getGlobalContextMenuItems(&items);

				for (std::string& it : items) {
					if (ImGui::MenuItem(it.c_str(), NULL, false, true)) {
						backend->onGlobalContextMenuSelected(it);
					}					
				}
			}
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();

		// Scrolling
		if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() && ImGui::IsMouseDragging(2, 0.0f))
			scrolling = scrolling - ImGui::GetIO().MouseDelta;

		/*ImGui::PopItemWidth();
		ImGui::EndChild();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(2);*/
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
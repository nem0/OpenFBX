#include "imgui/imgui.h"
#include "ofbx.h"
#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <memory>
#include <vector>
#pragma comment(lib, "opengl32.lib")


HWND g_hWnd;
HDC g_hDC;
HGLRC g_hRC;
GLuint g_font_texture;
typedef char Path[MAX_PATH];
typedef unsigned int u32;
ofbx::IScene* g_scene = nullptr;
ofbx::IElement* g_selected_element = nullptr;
ofbx::Object* g_selected_object = nullptr;


template <int N>
void toString(ofbx::DataView view, char (&out)[N])
{
	int len = int(view.end - view.begin);
	if (len > sizeof(out) - 1) len = sizeof(out) - 1;
	strncpy(out, (const char*)view.begin, len);
	out[len] = 0;
}


int getPropertyCount(ofbx::IElementProperty* prop)
{
	return prop ? getPropertyCount(prop->getNext()) + 1 : 0;
}


template <int N>
void catProperty(char(&out)[N], const ofbx::IElementProperty& prop)
{
	char tmp[128];
	switch (prop.getType())
	{
		case ofbx::IElementProperty::DOUBLE: sprintf_s(tmp, "%f", prop.getValue().toDouble()); break;
		case ofbx::IElementProperty::LONG: sprintf_s(tmp, "%" PRId64, prop.getValue().toLong()); break;
		case ofbx::IElementProperty::STRING: prop.getValue().toString(tmp); break;
		default: sprintf_s(tmp, "Type: %c", (char)prop.getType()); break;
	}
	strcat_s(out, tmp);
}


void showGUI(ofbx::IElement& element)
{
	auto id = element.getID();
	char label[128];
	id.toString(label);
	strcat_s(label, " (");
	ofbx::IElementProperty* prop = element.getFirstProperty();
	bool first = true;
	while (prop)
	{
		if(!first)
			strcat_s(label, ", ");
		first = false;
		catProperty(label, *prop);
		prop = prop->getNext();
	}
	strcat_s(label, ")");

	ImGui::PushID((const void*)id.begin);
	ImGuiTreeElementFlags flags = g_selected_element == &element ? ImGuiTreeElementFlags_Selected : 0;
	if (!element.getFirstChild()) flags |= ImGuiTreeElementFlags_Leaf;
	if (ImGui::TreeElementEx(label, flags))
	{
		if(ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) g_selected_element = &element;
		if (element.getFirstChild()) showGUI(*element.getFirstChild());
		ImGui::TreePop();
	}
	else
	{
		if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) g_selected_element = &element;
	}
	ImGui::PopID();
	if (element.getSibling()) showGUI(*element.getSibling());
}


void showArrayDouble(ofbx::IElementProperty& prop)
{
	if (!ImGui::CollapsingHeader("Double Array")) return;

	static bool as_vec3 = false;
	ImGui::Checkbox("As Vec3", &as_vec3);

	int count = prop.getCount();

	ImGui::Text("Count: %d", count);
	std::vector<double> tmp;
	tmp.resize(count);
	prop.getValues(&tmp[0], int(sizeof(tmp[0]) * tmp.size()));
	if (as_vec3)
	{
		for (int i = 0; i < tmp.size(); i += 3)
		{
			ImGui::Text("%f %f %f", tmp[i], tmp[i + 1], tmp[i + 2]);
		}
		return;
	}
	for (double v : tmp)
	{
		ImGui::Text("%f", v);
	}
}


void showArrayInt(ofbx::IElementProperty& prop)
{
	if (!ImGui::CollapsingHeader("Int Array")) return;

	int count = prop.getCount();
	ImGui::Text("Count: %d", count);
	std::vector<int> tmp;
	tmp.resize(count);
	prop.getValues(&tmp[0], int(sizeof(tmp[0]) * tmp.size()));
	for (int v : tmp)
	{
		ImGui::Text("%d", v);
	}
}


void showGUI(ofbx::IElementProperty& prop)
{
	ImGui::PushID((void*)&prop);
	char tmp[256];
	switch (prop.getType())
	{
		case ofbx::IElementProperty::LONG: ImGui::Text("Long: %" PRId64, *(ofbx::u64*)prop.getValue().begin); break;
		case ofbx::IElementProperty::FLOAT: ImGui::Text("Float: %f", *(float*)prop.getValue().begin); break;
		case ofbx::IElementProperty::DOUBLE: ImGui::Text("Double: %f", *(double*)prop.getValue().begin); break;
		case ofbx::IElementProperty::INTEGER: ImGui::Text("Integer: %d", *(int*)prop.getValue().begin); break;
		case ofbx::IElementProperty::ARRAY_DOUBLE: showArrayDouble(prop); break;
		case ofbx::IElementProperty::ARRAY_INT: showArrayInt(prop); break;
		case ofbx::IElementProperty::STRING:
			toString(prop.getValue(), tmp);
			ImGui::Text("String: %s", tmp);
			break;
		default:
			ImGui::Text("Other: %c", (char)prop.getType());
			break;
	}

	ImGui::PopID();
	if (prop.getNext()) showGUI(*prop.getNext());
}


void showObjectGUI(ofbx::Object& object)
{
	const char* label;
	switch (object.getType())
	{
		case ofbx::Object::GEOMETRY: label = "geometry"; break;
		case ofbx::Object::MESH: label = "mesh"; break;
		case ofbx::Object::MATERIAL: label = "material"; break;
		case ofbx::Object::ROOT: label = "root"; break;
		case ofbx::Object::TEXTURE: label = "texture"; break;
		case ofbx::Object::NULL_NODE: label = "texture"; break;
		case ofbx::Object::LIMB_NODE: label = "limb node"; break;
		case ofbx::Object::NOTE_ATTRIBUTE: label = "node attribute"; break;
		case ofbx::Object::CLUSTER: label = "cluster"; break;
		case ofbx::Object::SKIN: label = "skin"; break;
		default: assert(false); break;
	}

	ImGuiTreeElementFlags flags = g_selected_object == &object ? ImGuiTreeElementFlags_Selected : 0;
	char tmp[128];
	ofbx::IElementProperty* prop = object.element.getFirstProperty();
	ofbx::u64 id = prop ? prop->getValue().toLong() : 0;
	sprintf_s(tmp, "%" PRId64 " %s", id, label);
	if (ImGui::TreeElementEx(tmp, flags))
	{
		if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) g_selected_object = &object;
		int i = 0;
		while (ofbx::Object* child = object.resolveObjectLink(i))
		{
			showObjectGUI(*child);
			++i;
		}
		ImGui::TreePop();
	}
	else
	{
		if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) g_selected_object = &object;
	}
}


void showObjectsGUI(const ofbx::IScene& scene)
{
	if (!ImGui::Begin("Objects"))
	{
		ImGui::End();
		return;
	}
	ofbx::Object* root = scene.getRoot();
	if (root) showObjectGUI(*root);

	ImGui::End();
}


bool saveAsOBJ(ofbx::IScene& scene, const char* path)
{
	FILE* fp = fopen(path, "wb");
	if (!fp) return false;
	int obj_idx = 0;
	int indices_offset = 0;
	int normals_offset = 0;
	int mesh_count = scene.resolveObjectCount(ofbx::Object::GEOMETRY);
	for (int i = 0; i < mesh_count; ++i)
	{
		fprintf(fp, "o obj%d\ng grp%d\n", i, obj_idx);

		const ofbx::Geometry& mesh = (const ofbx::Geometry&)*scene.resolveObject(ofbx::Object::GEOMETRY, i);
		int vertex_count = mesh.getVertexCount();
		const ofbx::Vec3* vertices = mesh.getVertices();
		for (int i = 0; i < vertex_count; ++i)
		{
			ofbx::Vec3 v = vertices[i];
			fprintf(fp, "v %f %f %f\n", v.x, v.y, v.z);
		}

		int index_count = mesh.getIndexCount();
		
		bool has_normals = mesh.getNormalCount() > 0;
		if (has_normals)
		{
			std::vector<ofbx::Vec3> normals;
			normals.resize(index_count);
			mesh.resolveNormals(&normals[0]);

			for (ofbx::Vec3 n : normals)
			{
				fprintf(fp, "vn %f %f %f\n", n.x, n.y, n.z);
			}
		}

		bool has_uvs = mesh.getUVCount() > 0;
		if (has_uvs)
		{
			std::vector<ofbx::Vec2> uvs;
			uvs.resize(index_count);
			mesh.resolveUVs(&uvs[0]);

			for (ofbx::Vec2 uv : uvs)
			{
				fprintf(fp, "vt %f %f\n", uv.x, uv.y);
			}
		}

		const int* indices = mesh.getIndices();
		bool new_face = true;
		for (int i = 0; i < index_count; ++i)
		{
			if (new_face)
			{
				fputs("f ", fp);
				new_face = false;
			}
			int idx = indices[i];
			int vertex_idx = indices_offset + (idx >= 0 ? idx + 1 : -idx);
			fprintf(fp, "%d", vertex_idx);

			if (has_normals)
			{
				fprintf(fp, "/%d", normals_offset + i + 1);
			}
			else
			{
				fprintf(fp, "/");
			}

			if (has_uvs)
			{
				fprintf(fp, "/%d", normals_offset + i + 1);
			}
			else
			{
				fprintf(fp, "/");
			}

			new_face = idx < 0;
			fputc(new_face ? '\n' : ' ', fp);
		}

		indices_offset += vertex_count;
		normals_offset += index_count;
		++obj_idx;
	}
	fclose(fp);
	return true;
}


void onGUI()
{
	auto& io = ImGui::GetIO();
	io.KeyShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	io.KeyCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	io.KeyCtrl = (GetKeyState(VK_MENU) & 0x8000) != 0;

	ImGui::NewFrame();
	ImGui::RootDock(ImVec2(0, 0), ImGui::GetIO().DisplaySize);
	if (ImGui::Begin("Elements"))
	{
		ofbx::IElement* root = g_scene->getRootElement();
		if (root && root->getFirstChild()) showGUI(*root->getFirstChild());
	}
	ImGui::End();

	if (ImGui::Begin("Properties") && g_selected_element)
	{
		ofbx::IElementProperty* prop = g_selected_element->getFirstProperty();
		if(prop) showGUI(*prop);
	}
	ImGui::End();

	showObjectsGUI(*g_scene);

	ImGui::Render();
}


void onResize(int width, int height)
{
	auto& io = ImGui::GetIO();
	io.DisplaySize.x = (float)width;
	io.DisplaySize.y = (float)height;
}


LRESULT WINAPI WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
		case WM_SYSCOMMAND:
		{
			bool is_alt_key_menu = wParam == SC_KEYMENU && (lParam >> 16) <= 0;
			if (is_alt_key_menu) return 0;
			break;
		}
		case WM_KEYUP:
		case WM_SYSKEYUP: ImGui::GetIO().KeysDown[(int)wParam] = false; break;
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN: ImGui::GetIO().KeysDown[(int)wParam] = true; break;
		case WM_CHAR: ImGui::GetIO().AddInputCharacter((int)wParam); break;
		case WM_RBUTTONDOWN: ImGui::GetIO().MouseDown[1] = true; break;
		case WM_RBUTTONUP: ImGui::GetIO().MouseDown[1] = false; break;
		case WM_LBUTTONDOWN: ImGui::GetIO().MouseDown[0] = true; break;
		case WM_LBUTTONUP: ImGui::GetIO().MouseDown[0] = false; break;
		case WM_MOUSEMOVE:
		{
			POINT p;
			p.x = ((int)(short)LOWORD(lParam));
			p.y = ((int)(short)HIWORD(lParam));
			ImGuiIO& io = ImGui::GetIO();
			io.MousePos.x = (float)p.x;
			io.MousePos.y = (float)p.y;
		}
		break;
		case WM_SIZE:
		{
			RECT rect;
			GetClientRect(hWnd, &rect);
			onResize(rect.right - rect.left, rect.bottom - rect.top); 
			break;
		}
		case WM_CLOSE: PostQuitMessage(0); return 0;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

HWND CreateOpenGLWindow(char* title, int x, int y, int width, int height, BYTE type, DWORD flags)
{
	WNDCLASS wc;
	wc.style = CS_OWNDC;
	wc.lpfnWndProc = (WNDPROC)WindowProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = GetModuleHandle(NULL);
	wc.hIcon = LoadIcon(NULL, IDI_WINLOGO);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = NULL;
	wc.lpszMenuName = NULL;
	wc.lpszClassName = "ImGUI";

	if (!RegisterClass(&wc)) return NULL;

	HWND hWnd = CreateWindow("ImGUI",
		title,
		WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
		x,
		y,
		width,
		height,
		NULL,
		NULL,
		wc.hInstance,
		NULL);

	if (hWnd == NULL) return NULL;

	g_hDC = GetDC(hWnd);
	PIXELFORMATDESCRIPTOR pfd = {};
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DOUBLEBUFFER | PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | flags;
	pfd.iPixelType = type;
	pfd.cColorBits = 32;

	int pf = ChoosePixelFormat(g_hDC, &pfd);
	if (pf == 0) return NULL;
	if (SetPixelFormat(g_hDC, pf, &pfd) == FALSE) return NULL;

	DescribePixelFormat(g_hDC, pf, sizeof(PIXELFORMATDESCRIPTOR), &pfd);
	ReleaseDC(hWnd, g_hDC);
	return hWnd;
}


void imGUICallback(ImDrawData* draw_data)
{
	ImGuiIO& io = ImGui::GetIO();
	int fb_width = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
	int fb_height = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
	if (fb_width == 0 || fb_height == 0)
		return;
	draw_data->ScaleClipRects(io.DisplayFramebufferScale);

	GLint last_texture;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
	GLint last_viewport[4];
	glGetIntegerv(GL_VIEWPORT, last_viewport);
	glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TRANSFORM_BIT);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_SCISSOR_TEST);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glEnable(GL_TEXTURE_2D);

	glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0.0f, io.DisplaySize.x, io.DisplaySize.y, 0.0f, -1.0f, +1.0f);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	#define OFFSETOF(TYPE, ELEMENT) ((size_t) & (((TYPE*)0)->ELEMENT))
	for (int n = 0; n < draw_data->CmdListsCount; n++)
	{
		const ImDrawList* cmd_list = draw_data->CmdLists[n];
		const unsigned char* vtx_buffer = (const unsigned char*)&cmd_list->VtxBuffer.front();
		const ImDrawIdx* idx_buffer = &cmd_list->IdxBuffer.front();
		glVertexPointer(
			2, GL_FLOAT, sizeof(ImDrawVert), (void*)(vtx_buffer + OFFSETOF(ImDrawVert, pos)));
		glTexCoordPointer(
			2, GL_FLOAT, sizeof(ImDrawVert), (void*)(vtx_buffer + OFFSETOF(ImDrawVert, uv)));
		glColorPointer(4,
			GL_UNSIGNED_BYTE,
			sizeof(ImDrawVert),
			(void*)(vtx_buffer + OFFSETOF(ImDrawVert, col)));

		for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.size(); cmd_i++)
		{
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
			if (pcmd->UserCallback)
			{
				pcmd->UserCallback(cmd_list, pcmd);
			}
			else
			{
				GLuint texture_id =
					pcmd->TextureId ? (GLuint)(intptr_t)pcmd->TextureId : g_font_texture;

				glBindTexture(GL_TEXTURE_2D, texture_id);
				glScissor((int)pcmd->ClipRect.x,
					(int)(fb_height - pcmd->ClipRect.w),
					(int)(pcmd->ClipRect.z - pcmd->ClipRect.x),
					(int)(pcmd->ClipRect.w - pcmd->ClipRect.y));
				glDrawElements(GL_TRIANGLES,
					(GLsizei)pcmd->ElemCount,
					sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
					idx_buffer);
			}
			idx_buffer += pcmd->ElemCount;
		}
	}
	#undef OFFSETOF

	// Restore modified state
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
	glBindTexture(GL_TEXTURE_2D, last_texture);
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glPopAttrib();
	glViewport(last_viewport[0], last_viewport[1], (GLsizei)last_viewport[2], (GLsizei)last_viewport[3]);
}


void initImGUI()
{
	ImGuiIO& io = ImGui::GetIO();
	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);
	glEnable(GL_TEXTURE_2D);
	glGenTextures(1, &g_font_texture);
	glBindTexture(GL_TEXTURE_2D, g_font_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, width, height, 0, GL_ALPHA, GL_UNSIGNED_BYTE, pixels);

	io.KeyMap[ImGuiKey_Tab] = VK_TAB;
	io.KeyMap[ImGuiKey_LeftArrow] = VK_LEFT;
	io.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
	io.KeyMap[ImGuiKey_UpArrow] = VK_UP;
	io.KeyMap[ImGuiKey_DownArrow] = VK_DOWN;
	io.KeyMap[ImGuiKey_PageUp] = VK_PRIOR;
	io.KeyMap[ImGuiKey_PageDown] = VK_NEXT;
	io.KeyMap[ImGuiKey_Home] = VK_HOME;
	io.KeyMap[ImGuiKey_End] = VK_END;
	io.KeyMap[ImGuiKey_Delete] = VK_DELETE;
	io.KeyMap[ImGuiKey_Backspace] = VK_BACK;
	io.KeyMap[ImGuiKey_Enter] = VK_RETURN;
	io.KeyMap[ImGuiKey_Escape] = VK_ESCAPE;
	io.KeyMap[ImGuiKey_A] = 'A';
	io.KeyMap[ImGuiKey_C] = 'C';
	io.KeyMap[ImGuiKey_V] = 'V';
	io.KeyMap[ImGuiKey_X] = 'X';
	io.KeyMap[ImGuiKey_Y] = 'Y';
	io.KeyMap[ImGuiKey_Z] = 'Z';
	io.RenderDrawListsFn = imGUICallback;
}


bool init()
{
	g_hWnd = CreateOpenGLWindow("minimal", 0, 0, 800, 600, PFD_TYPE_RGBA, 0);
	if (g_hWnd == NULL) return false;

	g_hDC = GetDC(g_hWnd);
	g_hRC = wglCreateContext(g_hDC);
	wglMakeCurrent(g_hDC, g_hRC);

	ShowWindow(g_hWnd, SW_SHOW);
	initImGUI();

	FILE* fp = fopen("c.fbx", "rb");
	if (!fp) return false;

	fseek(fp, 0, SEEK_END);
	long file_size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	auto* content = new ofbx::u8[file_size];
	fread(content, 1, file_size, fp);
	g_scene = ofbx::load((ofbx::u8*)content, file_size);
	saveAsOBJ(*g_scene, "out.obj");
	delete[] content;
	fclose(fp);

	return true;
}


INT WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, INT)
{
	init();

	bool finished = false;
	while (!finished)
	{
		MSG msg;
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			if (msg.message == WM_QUIT) finished = true;
		}
		onGUI();
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui::Render();
		SwapBuffers(g_hDC);
	}

	wglMakeCurrent(NULL, NULL);
	ReleaseDC(g_hWnd, g_hDC);
	wglDeleteContext(g_hRC);
	DestroyWindow(g_hWnd);

	return 0;
}
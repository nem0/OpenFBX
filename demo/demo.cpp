#include "../src/ofbx.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include <stdio.h>
#include <inttypes.h>
#include <vector>

ofbx::IScene* g_scene = nullptr;
const ofbx::IElement* g_selected_element = nullptr;
const ofbx::Object* g_selected_object = nullptr;

template <int N>
void catProperty(char(&out)[N], const ofbx::IElementProperty& prop)
{
	char tmp[128];
	switch (prop.getType())
	{
		case ofbx::IElementProperty::DOUBLE: sprintf_s(tmp, "%f", prop.getValue().toDouble()); break;
		case ofbx::IElementProperty::LONG: sprintf_s(tmp, "%" PRId64, prop.getValue().toU64()); break;
		case ofbx::IElementProperty::INTEGER: sprintf_s(tmp, "%d", prop.getValue().toInt()); break;
		case ofbx::IElementProperty::STRING: prop.getValue().toString(tmp); break;
		default: sprintf_s(tmp, "Type: %c", (char)prop.getType()); break;
	}
	strcat_s(out, tmp);
}

void gui(const ofbx::IElement& parent) {
	for (const ofbx::IElement* element = parent.getFirstChild(); element; element = element->getSibling()) {
		auto id = element->getID();
		char label[128];
		id.toString(label);
		strcat_s(label, " (");
		ofbx::IElementProperty* prop = element->getFirstProperty();
		bool first = true;
		while (prop)
		{
			if (!first)
				strcat_s(label, ", ");
			first = false;
			catProperty(label, *prop);
			prop = prop->getNext();
		}
		strcat_s(label, ")");

		ImGui::PushID((const void*)id.begin);
		ImGuiTreeNodeFlags flags = g_selected_element == element ? ImGuiTreeNodeFlags_Selected : 0;
		if (!element->getFirstChild()) flags |= ImGuiTreeNodeFlags_Leaf;
		if (ImGui::TreeNodeEx(label, flags)) {
			if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) g_selected_element = element;
			if (element->getFirstChild()) gui(*element);
			ImGui::TreePop();
		}
		else
		{
			if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) g_selected_element = element;
		}
		ImGui::PopID();
	}
}

template <int N>
void toString(ofbx::DataView view, char (&out)[N])
{
	int len = int(view.end - view.begin);
	if (len > sizeof(out) - 1) len = sizeof(out) - 1;
	strncpy(out, (const char*)view.begin, len);
	out[len] = 0;
}

template <typename T>
void showArray(const char* label, const char* format, ofbx::IElementProperty& prop)
{
	if (!ImGui::CollapsingHeader(label)) return;

	int count = prop.getCount();
	ImGui::Text("Count: %d", count);
	std::vector<T> tmp;
	tmp.resize(count);
	prop.getValues(&tmp[0], int(sizeof(tmp[0]) * tmp.size()));
	for (T v : tmp)
	{
		ImGui::Text(format, v);
	}
}


void gui(ofbx::IElementProperty& prop)
{
	ImGui::PushID((void*)&prop);
	char tmp[256];
	switch (prop.getType())
	{
		case ofbx::IElementProperty::LONG: ImGui::Text("Long: %" PRId64, prop.getValue().toU64()); break;
		case ofbx::IElementProperty::FLOAT: ImGui::Text("Float: %f", prop.getValue().toFloat()); break;
		case ofbx::IElementProperty::DOUBLE: ImGui::Text("Double: %f", prop.getValue().toDouble()); break;
		case ofbx::IElementProperty::INTEGER: ImGui::Text("Integer: %d", prop.getValue().toInt()); break;
		case ofbx::IElementProperty::ARRAY_FLOAT: showArray<float>("float array", "%f", prop); break;
		case ofbx::IElementProperty::ARRAY_DOUBLE: showArray<double>("double array", "%f", prop); break;
		case ofbx::IElementProperty::ARRAY_INT: showArray<int>("int array", "%d", prop); break;
		case ofbx::IElementProperty::ARRAY_LONG: showArray<ofbx::u64>("long array", "%" PRId64, prop); break;
		case ofbx::IElementProperty::STRING:
			toString(prop.getValue(), tmp);
			ImGui::Text("String: %s", tmp);
			break;
		default:
			ImGui::Text("Other: %c", (char)prop.getType());
			break;
	}

	ImGui::PopID();
	if (prop.getNext()) gui(*prop.getNext());
}

static void guiCurve(const ofbx::Object& object) {
    const ofbx::AnimationCurve& curve = static_cast<const ofbx::AnimationCurve&>(object);
    
    const int c = curve.getKeyCount();
    for (int i = 0; i < c; ++i) {
        const float t = (float)ofbx::fbxTimeToSeconds(curve.getKeyTime()[i]);
        const float v = curve.getKeyValue()[i];
        ImGui::Text("%fs: %f ", t, v);
        
    }
}

void guiObjects(const ofbx::Object& object)
{
	const char* label;
	switch (object.getType())
	{
		case ofbx::Object::Type::GEOMETRY: label = "geometry"; break;
		case ofbx::Object::Type::MESH: label = "mesh"; break;
		case ofbx::Object::Type::MATERIAL: label = "material"; break;
		case ofbx::Object::Type::ROOT: label = "root"; break;
		case ofbx::Object::Type::TEXTURE: label = "texture"; break;
		case ofbx::Object::Type::NULL_NODE: label = "null"; break;
		case ofbx::Object::Type::LIMB_NODE: label = "limb node"; break;
		case ofbx::Object::Type::NODE_ATTRIBUTE: label = "node attribute"; break;
		case ofbx::Object::Type::CLUSTER: label = "cluster"; break;
		case ofbx::Object::Type::SKIN: label = "skin"; break;
		case ofbx::Object::Type::ANIMATION_STACK: label = "animation stack"; break;
		case ofbx::Object::Type::ANIMATION_LAYER: label = "animation layer"; break;
		case ofbx::Object::Type::ANIMATION_CURVE: label = "animation curve"; break;
		case ofbx::Object::Type::ANIMATION_CURVE_NODE: label = "animation curve node"; break;
		case ofbx::Object::Type::LIGHT: label = "light"; break;
		case ofbx::Object::Type::CAMERA: label = "camera"; break;
		default: assert(false); break;
	}

	ImGuiTreeNodeFlags flags = g_selected_object == &object ? ImGuiTreeNodeFlags_Selected : 0;
	char tmp[128];
	sprintf_s(tmp, "%" PRId64 " %s (%s)", object.id, object.name, label);
	if (ImGui::TreeNodeEx(tmp, flags))
	{
		if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) g_selected_object = &object;
		int i = 0;
		while (ofbx::Object* child = object.resolveObjectLink(i))
		{
			guiObjects(*child);
			++i;
		}
        if(object.getType() == ofbx::Object::Type::ANIMATION_CURVE) {
            guiCurve(object);
        }

		ImGui::TreePop();
	}
	else
	{
		if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) g_selected_object = &object;
	}
}

void guiObjects(const ofbx::IScene& scene)
{
	if (!ImGui::Begin("Objects"))
	{
		ImGui::End();
		return;
	}
	const ofbx::Object* root = scene.getRoot();
	if (root) guiObjects(*root);

	int count = scene.getAnimationStackCount();
	for (int i = 0; i < count; ++i)
	{
		const ofbx::Object* stack = scene.getAnimationStack(i);
		guiObjects(*stack);
	}

	ImGui::End();
}


void demoGUI() {
	if (ImGui::Begin("Elements")) {
		const ofbx::IElement* root = g_scene->getRootElement();
		if (root && root->getFirstChild()) gui(*root);
	}
	ImGui::End();

	if (ImGui::Begin("Properties") && g_selected_element)
	{
		ofbx::IElementProperty* prop = g_selected_element->getFirstProperty();
		if (prop) gui(*prop);
	}
	ImGui::End();
	guiObjects(*g_scene);
}


bool saveAsOBJ(ofbx::IScene& scene, const char* path)
{
	FILE* fp = fopen(path, "wb");
	if (!fp) return false;
	int indices_offset = 0;
	int mesh_count = scene.getMeshCount();
	
	// output unindexed geometry
	for (int mesh_idx = 0; mesh_idx < mesh_count; ++mesh_idx) {
		const ofbx::Mesh& mesh = *scene.getMesh(mesh_idx);
		const ofbx::GeometryData& geom = mesh.getGeometryData();
		const ofbx::Vec3Attributes positions = geom.getPositions();
		const ofbx::Vec3Attributes normals = geom.getNormals();
		const ofbx::Vec2Attributes uvs = geom.getUVs();

		// each ofbx::Mesh can have several materials == partitions
		for (int partition_idx = 0; partition_idx < geom.getPartitionCount(); ++partition_idx) {
			fprintf(fp, "o obj%d_%d\ng grp%d\n", mesh_idx, partition_idx, mesh_idx);
			const ofbx::GeometryPartition& partition = geom.getPartition(partition_idx);
		
			// partitions most likely have several polygons, they are not triangles necessarily, use ofbx::triangulate if you want triangles
			for (int polygon_idx = 0; polygon_idx < partition.polygon_count; ++polygon_idx) {
				const ofbx::GeometryPartition::Polygon& polygon = partition.polygons[polygon_idx];
				
				for (int i = polygon.from_vertex; i < polygon.from_vertex + polygon.vertex_count; ++i) {
					ofbx::Vec3 v = positions.get(i);
					fprintf(fp, "v %f %f %f\n", v.x, v.y, v.z);
				}

				bool has_normals = normals.values != nullptr;
				if (has_normals) {
					// normals.indices might be different than positions.indices
					// but normals.get(i) is normal for positions.get(i)
					for (int i = polygon.from_vertex; i < polygon.from_vertex + polygon.vertex_count; ++i) {
						ofbx::Vec3 n = normals.get(i);
						fprintf(fp, "vn %f %f %f\n", n.x, n.y, n.z);
					}
				}

				bool has_uvs = uvs.values != nullptr;
				if (has_uvs) {
					for (int i = polygon.from_vertex; i < polygon.from_vertex + polygon.vertex_count; ++i) {
						ofbx::Vec2 uv = uvs.get(i);
						fprintf(fp, "vt %f %f\n", uv.x, uv.y);
					}
				}
			}

			for (int polygon_idx = 0; polygon_idx < partition.polygon_count; ++polygon_idx) {
				const ofbx::GeometryPartition::Polygon& polygon = partition.polygons[polygon_idx];
				fputs("f ", fp);
				for (int i = polygon.from_vertex; i < polygon.from_vertex + polygon.vertex_count; ++i) {
					fprintf(fp, "%d ", 1 + i + indices_offset);
				}
				fputs("\n", fp);
			}

			indices_offset += positions.count;
		}
	}
	fclose(fp);
	return true;
}


bool initOpenFBX(const char* filepath, HWND hwnd) {
	static char s_TimeString[256];
	FILE* fp = fopen(filepath, "rb");

	if (!fp) return false;

	fseek(fp, 0, SEEK_END);
	long file_size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	auto* content = new ofbx::u8[file_size];
	fread(content, 1, file_size, fp);

	// Start Timer
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	LARGE_INTEGER start;
	QueryPerformanceCounter(&start);

	// Ignoring certain nodes will only stop them from being processed not tokenised (i.e. they will still be in the tree)
	ofbx::LoadFlags flags =
//		ofbx::LoadFlags::IGNORE_MODELS |
		ofbx::LoadFlags::IGNORE_BLEND_SHAPES |
		ofbx::LoadFlags::IGNORE_CAMERAS |
		ofbx::LoadFlags::IGNORE_LIGHTS |
//		ofbx::LoadFlags::IGNORE_TEXTURES |
		ofbx::LoadFlags::IGNORE_SKIN |
		ofbx::LoadFlags::IGNORE_BONES |
		ofbx::LoadFlags::IGNORE_PIVOTS |
//		ofbx::LoadFlags::IGNORE_MATERIALS |
		ofbx::LoadFlags::IGNORE_POSES |
		ofbx::LoadFlags::IGNORE_VIDEOS |
		ofbx::LoadFlags::IGNORE_LIMBS |
//		ofbx::LoadFlags::IGNORE_MESHES |
		ofbx::LoadFlags::IGNORE_ANIMATIONS;

	g_scene = ofbx::load((ofbx::u8*)content, file_size, (ofbx::u16)flags);

	// Stop Timer
	LARGE_INTEGER stop;
	QueryPerformanceCounter(&stop);
	double elapsed = (double)(stop.QuadPart - start.QuadPart) / (double)frequency.QuadPart;
	snprintf(s_TimeString,
    sizeof(s_TimeString),
    "Loading took %f seconds (%.0f ms) to load %s file of size %d bytes (%f MB) \r ",
    elapsed,
    elapsed * 1000.0,
    filepath,
    file_size,
    (double)file_size / (1024.0 * 1024.0));


	if(!g_scene) {
        OutputDebugString(ofbx::getError());
    }
    else {
        //saveAsOBJ(*g_scene, "out.obj");
    }
	delete[] content;
	fclose(fp);
	saveAsOBJ(*g_scene, "out.obj");

	// Make the window title s_TimeString
	SetWindowText(hwnd, s_TimeString);
	return true;
}

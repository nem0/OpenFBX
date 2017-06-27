#include "ofbx.h"
#include "miniz.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <vector>


namespace ofbx
{


struct Vec3
{
	float x, y, z;
};


struct Vec3d
{
	double x, y, z;
};


#pragma pack(1)
struct Header
{
	u8 magic[21];
	u8 reserved[2];
	u32 version;
};
#pragma pack()


struct Cursor
{
	const u8* current;
	const u8* begin;
	const u8* end;
};

struct Property;
template <typename T> void parseBinaryArray(const Property& property, T* out);


struct Property : IProperty
{
	~Property() { delete next; }
	Type getType() const override { return (Type)type; }
	IProperty* getNext() const override { return next; }
	DataView getValue() const override { return value; }
	int getCount() const override 
	{
		assert(type == ARRAY_DOUBLE || type == ARRAY_INT);
		return int(*(u32*)value.begin);
	}

	void getValues(double* values) const override
	{
		parseBinaryArray(*this, values);
	}

	void getValues(int* values) const override
	{
		parseBinaryArray(*this, values);
	}

	u8 type;
	DataView value;
	Property* next = nullptr;
};


struct Node : INode
{
	~Node()
	{
		delete child;
		delete sibling;
		delete first_property;
	}

	INode* getFirstChild() const override { return child; }
	INode* getSibling() const override { return sibling; }
	DataView getID() const override { return id; }
	IProperty* getFirstProperty() const override { return first_property; }

	DataView id;
	Node* child = nullptr;
	Node* sibling = nullptr;
	Property* first_property = nullptr;
};


void decompress(const u8* in, size_t in_size, u8* out, size_t out_size)
{
	mz_stream stream = {};
	mz_inflateInit(&stream);

	int status;
	stream.avail_in = (int)in_size;
	stream.next_in = in;
	stream.avail_out = (int)out_size;
	stream.next_out = out;

	status = mz_inflate(&stream, Z_SYNC_FLUSH);

	assert(status == Z_STREAM_END);

	if (mz_inflateEnd(&stream) != Z_OK)
	{
		printf("inflateEnd() failed!\n");
	}
}


u32 readWord(Cursor* cursor)
{
	u32 word = *(const u32*)cursor->current;
	cursor->current += 4;
	return word;
}


u8 readByte(Cursor* cursor)
{
	u8 byte = *(const u8*)cursor->current;
	cursor->current += 1;
	return byte;
}


DataView readShortString(Cursor* cursor)
{
	DataView value;
	const u8 length = readByte(cursor);

	value.begin = cursor->current;
	cursor->current += length;

	value.end = cursor->current;

	return value;
}


DataView readLongString(Cursor* cursor)
{
	DataView value;
	const u32 length = readWord(cursor);

	value.begin = cursor->current;
	cursor->current += length;

	value.end = cursor->current;

	return value;
}


Property* readProperty(Cursor* cursor)
{
	Property* prop = new Property;
	prop->next = nullptr;
	prop->type = *cursor->current;
	++cursor->current;
	prop->value.begin = cursor->current;

	switch (prop->type)
	{
		case 'S': prop->value = readLongString(cursor); break;
		case 'Y': cursor->current += 2; break;
		case 'C': cursor->current += 1; break;
		case 'I': cursor->current += 4; break;
		case 'F': cursor->current += 4; break;
		case 'D': cursor->current += 8; break;
		case 'L': cursor->current += 8; break;
		case 'R':
		{
			u32 len = readWord(cursor);
			cursor->current += len;
			break;
		}
		case 'b':
		case 'f':
		case 'd':
		case 'l':
		case 'i':
		{
			u32 length = readWord(cursor);
			u32 encoding = readWord(cursor);
			u32 comp_len = readWord(cursor);
			cursor->current += comp_len;
			break;
		}
		default: assert(0); break;
	}
	prop->value.end = cursor->current;
	return prop;
}


Node* readNode(Cursor* cursor)
{
	u32 end_offset = readWord(cursor);
	if (end_offset == 0) return nullptr;
	u32 prop_count = readWord(cursor);
	u32 prop_length = readWord(cursor);

	const char* sbeg = 0;
	const char* send = 0;
	DataView id = readShortString(cursor);
	Node* node = new Node;
	node->first_property = nullptr;
	node->id = id;

	node->child = nullptr;
	node->sibling = nullptr;

	Property** prop_link = &node->first_property;
	for (u32 i = 0; i < prop_count; ++i)
	{
		*prop_link = readProperty(cursor);
		prop_link = &(*prop_link)->next;
	}

	if (cursor->current - cursor->begin >= end_offset) return node;

	enum
	{
		_BLOCK_SENTINEL_LENGTH = 13
	};

	Node** link = &node->child;
	while (cursor->current - cursor->begin < (end_offset - _BLOCK_SENTINEL_LENGTH))
	{
		*link = readNode(cursor);
		link = &(*link)->sibling;
	}

	cursor->current += _BLOCK_SENTINEL_LENGTH;
	return node;
}


Node* tokenize(const u8* data, size_t size)
{
	Cursor cursor;
	cursor.begin = data;
	cursor.current = data;
	cursor.end = data + size;

	const Header* header = (const Header*)cursor.current;
	cursor.current += sizeof(*header);

	Node* root = new Node;
	root->first_property = nullptr;
	root->id.begin = nullptr;
	root->id.end = nullptr;
	root->child = nullptr;
	root->sibling = nullptr;

	Node** node = &root->child;
	for (;;)
	{
		*node = readNode(&cursor);
		if (!*node) return root;
		node = &(*node)->sibling;
	}
	return root;
}


Node* findFirstNode(Node& node, const char* id)
{
	Node** iter = &node.child;
	while (*iter)
	{
		if ((*iter)->id == id) return *iter;
		iter = &(*iter)->sibling;
	}
	return nullptr;
}


void parseTemplates(Node& root)
{
	Node* defs = findFirstNode(root, "Definitions");
	if (!defs) return;

	std::unordered_map<std::string, Node*> templates;
	Node* def = defs->child;
	while (def)
	{
		if (def->id == "ObjectType")
		{
			Node* subdef = def->child;
			while (subdef)
			{
				if (subdef->id == "PropertyTemplate")
				{
					DataView prop1 = def->first_property->value;
					DataView prop2 = subdef->first_property->value;
					std::string key((const char*)prop1.begin, prop1.end - prop1.begin);
					key += std::string((const char*)prop1.begin, prop1.end - prop1.begin);
					templates[key] = subdef;
				}
				subdef = subdef->sibling;
			}
		}
		def = def->sibling;
	}
	// TODO
}


u64 getNodeUUID(const Node& node)
{
	assert(node.first_property);
	assert(node.first_property->type == 'L');
	return *(u64*)node.first_property->value.begin;
}


struct Mesh : IObject
{
	enum VertexDataMapping
	{
		BY_POLYGON_VERTEX,
		BY_POLYGON,
	};


	std::vector<Vec3> vertices;
	std::vector<Vec3> normals;
	VertexDataMapping normals_mapping;
	std::vector<int> indices;
	std::vector<int> normals_indices;
	Type getType() const override { return MESH; }
};


struct Scene : IScene
{
	struct Object
	{
		Node* node;
		IObject* object;
	};


	INode* getRoot() const override { return m_root.get(); }


	void destroy() override { delete this; }



	static int getNormalIndex(const Mesh& mesh, int vertex_idx, int idx)
	{
		if (mesh.normals_mapping == Mesh::BY_POLYGON_VERTEX)
		{
			return idx;
		}
		assert(false);
		return -1;
	}


	bool saveAsOBJ(const char* path) const override
	{
		FILE* fp = fopen(path, "wb");
		if (!fp) return false;
		int obj_idx = 0;
		int indices_offset = 0;
		int normals_offset = 0;
		for (auto iter : m_object_map)
		{
			if (iter.second.object == nullptr) continue;
			if (iter.second.object->getType() != IObject::MESH) continue;

			fprintf(fp, "o obj%d\ng grp%d\n", obj_idx, obj_idx);

			const Mesh& mesh = (const Mesh&)*iter.second.object;
			if (mesh.normals.empty())
			{
				fclose(fp);
				return false;
			}
			for (Vec3 v : mesh.vertices)
			{
				fprintf(fp, "v %f %f %f\n", v.x, v.y, v.z);
			}

			for (Vec3 v : mesh.normals)
			{
				fprintf(fp, "vn %f %f %f\n", v.x, v.y, v.z);
			}

			const std::vector<int>& idcs = mesh.indices;
			bool new_face = true;
			for (int i = 0, c = (int)idcs.size(); i < c; ++i)
			{
				if (new_face)
				{
					fputs("f ", fp);
					new_face = false;
				}
				int idx = idcs[i];
				int vertex_idx = idx >= 0 ? indices_offset + idx + 1 : indices_offset - idx;
				int normal_idx = normals_offset + getNormalIndex(mesh, vertex_idx, i) + 1;
				if (idx >= 0)
					fprintf(fp, "%d//%d ", vertex_idx, normal_idx);
				else
				{
					fprintf(fp, "%d//%d\n", vertex_idx, normal_idx);
					new_face = true;
				}
			}

			indices_offset += (int)mesh.vertices.size();
			normals_offset += (int)mesh.normals.size();

			++obj_idx;
		}
		fclose(fp);
		return true;
	}


	std::unique_ptr<Node> m_root;
	std::unordered_map<u64, Object> m_object_map;
	std::vector<u8> m_data;
};


void parseObjects(Node& root, Scene* scene)
{
	Node* objs = findFirstNode(root, "Objects");
	if (!objs) return;

	Node* object = objs->child;
	while (object)
	{
		u64 uuid = getNodeUUID(*object);
		scene->m_object_map[uuid] = {object, nullptr};
		object = object->sibling;
	}
}


Property* getLastProperty(Node* node)
{
	Property* prop = node->first_property;
	if (!prop) return nullptr;
	while (prop->next) prop = prop->next;
	return prop;
}


u32 getArrayCount(const Property& property)
{
	return *(const u32*)property.value.begin;
}



template <typename T> void parseBinaryArray(const Property& property, T* out)
{
	assert(out);
	u32 count = getArrayCount(property);
	u32 enc = *(const u32*)(property.value.begin + 4);
	u32 len = *(const u32*)(property.value.begin + 8);

	int elem_size = 1;
	switch (property.type)
	{
		case 'd': elem_size = 8; break;
		case 'f': elem_size = 4; break;
		case 'i': elem_size = 4; break;
		default: assert(false);
	}

	const u8* data = property.value.begin + sizeof(u32) * 3;
	if (enc == 0)
	{
		memcpy(out, data, len);
	}
	else if (enc == 1)
	{
		decompress(data, len, (u8*)out, elem_size * count);
	}
	else
	{
		assert(false);
	}
}


template <typename T> void parseBinaryArray(Property& property, std::vector<T>* out)
{
	assert(out);
	u32 count = getArrayCount(property);
	int elem_size = 1;
	switch (property.type)
	{
		case 'd': elem_size = 8; break;
		case 'f': elem_size = 4; break;
		case 'i': elem_size = 4; break;
		default: assert(false);
	}
	int elem_count = sizeof(T) / elem_size;
	out->resize(count / elem_count);

	parseBinaryArray(property, &(*out)[0]);
}


void parseVec3Data(Property& property, std::vector<Vec3>* out)
{
	if (property.type == 'f')
	{
		parseBinaryArray(property, out);
	}
	else
	{
		assert(property.type == 'd');
		std::vector<Vec3d> tmp;
		parseBinaryArray(property, &tmp);
		out->resize(tmp.size());
		for (int i = 0, c = (int)tmp.size(); i < c; ++i)
		{
			(*out)[i].x = (float)tmp[i].x;
			(*out)[i].y = (float)tmp[i].y;
			(*out)[i].z = (float)tmp[i].z;
		}
	}
}


template <typename T>
static void parseVertexData(Node& node, const char* name, const char* index_name, std::vector<T>* out, std::vector<int>* out_indices, Mesh::VertexDataMapping* mapping)
{
	assert(out);
	assert(mapping);
	Node* data_node = findFirstNode(node, name);
	if (data_node && data_node->first_property)
	{
		Node* mapping_node = findFirstNode(node, "MappingInformationType");
		Node* reference_node = findFirstNode(node, "ReferenceInformationType");
		
		if (mapping_node && mapping_node->first_property)
		{
			if (mapping_node->first_property->value == "ByPolygonVertex")
			{
				*mapping = Mesh::BY_POLYGON_VERTEX;
			}
			else if (mapping_node->first_property->value == "ByPolygon")
			{
				*mapping = Mesh::BY_POLYGON;
			}
			else
			{
				assert(false);
			}
		}
		if (reference_node && reference_node->first_property)
		{
			if (reference_node->first_property->value == "IndexToDirect")
			{
				Node* indices_node = findFirstNode(node, index_name);
				if (indices_node && indices_node->first_property)
				{
					parseBinaryArray(*indices_node->first_property, out_indices);
				}
			}
			else if (reference_node->first_property->value != "Direct")
			{
				assert(false);
			}
		}
		parseVec3Data(*data_node->first_property, out);

	}
}


Mesh* parseMesh(const Scene& scene, Node& node)
{
	Node* vertices_node = findFirstNode(node, "Vertices");
	if (!vertices_node || !vertices_node->first_property) return nullptr;

	Node* polys_node = findFirstNode(node, "PolygonVertexIndex");
	if (!polys_node || !polys_node->first_property) return nullptr;

	Mesh* mesh = new Mesh;

	parseVec3Data(*vertices_node->first_property, &mesh->vertices);
	parseBinaryArray(*polys_node->first_property, &mesh->indices);

	Node* layer_normal_node = findFirstNode(node, "LayerElementNormal");
	if (layer_normal_node)
	{
		parseVertexData(*layer_normal_node, "Normals", "NormalsIndex", &mesh->normals, &mesh->normals_indices, &mesh->normals_mapping);
	}

	// Node* uvs_node = findFirstNode(node, "LayerElementUV");


	return mesh;
}


void parseMeshes(Scene* scene)
{
	for (auto iter : scene->m_object_map)
	{
		if (iter.second.node->id != "Geometry") continue;

		Property* last_prop = getLastProperty(iter.second.node);
		if (last_prop && last_prop->value == "Mesh")
		{
			scene->m_object_map[iter.first].object = parseMesh(*scene, *iter.second.node);
		}
	}
}


IScene* load(const u8* data, size_t size)
{
	Scene* scene = new Scene;
	scene->m_data.resize(size);
	memcpy(&scene->m_data[0], data, size);
	Node* root = ofbx::tokenize(&scene->m_data[0], size);
	if (!root)
	{
		delete scene;
		return nullptr;
	}
	scene->m_root = std::unique_ptr<Node>{root};
	ofbx::parseTemplates(*root);
	ofbx::parseObjects(*root, scene);
	ofbx::parseMeshes(scene);
	scene->saveAsOBJ("out.obj");
	return scene;
}


} // namespace ofbx

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


u64 DataView::toLong() const
{
	assert(end - begin == sizeof(u64));
	return *(u64*)begin;
}


double DataView::toDouble() const
{
	assert(end - begin == sizeof(double));
	return *(double*)begin;
}


bool DataView::operator==(const char* rhs) const
{
	const char* c = rhs;
	const char* c2 = (const char*)begin;
	while (*c && c2 != (const char*)end)
	{
		if (*c != *c2) return 0;
		++c;
		++c2;
	}
	return c2 == (const char*)end && *c == '\0';
}


struct Property;
template <typename T> void parseBinaryArrayRaw(const Property& property, T* out, int max_size);
template <typename T> void parseBinaryArray(Property& property, std::vector<T>* out);


struct Property : IElementProperty
{
	~Property() { delete next; }
	Type getType() const override { return (Type)type; }
	IElementProperty* getNext() const override { return next; }
	DataView getValue() const override { return value; }
	int getCount() const override 
	{
		assert(type == ARRAY_DOUBLE || type == ARRAY_INT);
		return int(*(u32*)value.begin);
	}

	void getValues(double* values, int max_size) const override
	{
		parseBinaryArrayRaw(*this, values, max_size);
	}

	void getValues(int* values, int max_size) const override
	{
		parseBinaryArrayRaw(*this, values, max_size);
	}

	u8 type;
	DataView value;
	Property* next = nullptr;
};


struct Element : IElement
{
	~Element()
	{
		delete child;
		delete sibling;
		delete first_property;
	}

	IElement* getFirstChild() const override { return child; }
	IElement* getSibling() const override { return sibling; }
	DataView getID() const override { return id; }
	IElementProperty* getFirstProperty() const override { return first_property; }
	IElementProperty* getProperty(int idx) const
	{
		IElementProperty* prop = first_property;
		for (int i = 0; i < idx; ++i)
		{
			if (prop == nullptr) return nullptr;
			prop = prop->getNext();
		}
		return prop; 
	}

	DataView id;
	Element* child = nullptr;
	Element* sibling = nullptr;
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


Element* readElement(Cursor* cursor)
{
	u32 end_offset = readWord(cursor);
	if (end_offset == 0) return nullptr;
	u32 prop_count = readWord(cursor);
	u32 prop_length = readWord(cursor);

	const char* sbeg = 0;
	const char* send = 0;
	DataView id = readShortString(cursor);
	Element* element = new Element;
	element->first_property = nullptr;
	element->id = id;

	element->child = nullptr;
	element->sibling = nullptr;

	Property** prop_link = &element->first_property;
	for (u32 i = 0; i < prop_count; ++i)
	{
		*prop_link = readProperty(cursor);
		prop_link = &(*prop_link)->next;
	}

	if (cursor->current - cursor->begin >= end_offset) return element;

	enum
	{
		_BLOCK_SENTINEL_LENGTH = 13
	};

	Element** link = &element->child;
	while (cursor->current - cursor->begin < (end_offset - _BLOCK_SENTINEL_LENGTH))
	{
		*link = readElement(cursor);
		link = &(*link)->sibling;
	}

	cursor->current += _BLOCK_SENTINEL_LENGTH;
	return element;
}


Element* tokenize(const u8* data, size_t size)
{
	Cursor cursor;
	cursor.begin = data;
	cursor.current = data;
	cursor.end = data + size;

	const Header* header = (const Header*)cursor.current;
	cursor.current += sizeof(*header);

	Element* root = new Element;
	root->first_property = nullptr;
	root->id.begin = nullptr;
	root->id.end = nullptr;
	root->child = nullptr;
	root->sibling = nullptr;

	Element** element = &root->child;
	for (;;)
	{
		*element = readElement(&cursor);
		if (!*element) return root;
		element = &(*element)->sibling;
	}
	return root;
}


Element* findChild(Element& element, const char* id)
{
	Element** iter = &element.child;
	while (*iter)
	{
		if ((*iter)->id == id) return *iter;
		iter = &(*iter)->sibling;
	}
	return nullptr;
}


void parseTemplates(Element& root)
{
	Element* defs = findChild(root, "Definitions");
	if (!defs) return;

	std::unordered_map<std::string, Element*> templates;
	Element* def = defs->child;
	while (def)
	{
		if (def->id == "ObjectType")
		{
			Element* subdef = def->child;
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


u64 getElementUUID(const Element& element)
{
	assert(element.first_property);
	assert(element.first_property->type == 'L');
	return *(u64*)element.first_property->value.begin;
}


struct Scene;


struct MeshImpl : Mesh
{
	MeshImpl(const Scene& _scene, const IElement& _element)
		: Mesh(_scene, _element)
		, scene(_scene)
	{
	}

	Type getType() const override { return MODEL; }
	DataView getName() const override { return name; }

	DataView name;
	const Scene& scene;
};


struct MaterialImpl : Material
{
	MaterialImpl(const Scene& _scene, const IElement& _element)
		: Material(_scene, _element)
	{
	}
	Type getType() const override { return MATERIAL; }
	DataView getName() const override { return name; }

	DataView name;
};


struct LimbNodeImpl : LimbNode
{
	LimbNodeImpl(const Scene& _scene, const IElement& _element)
		: LimbNode(_scene, _element)
	{
	}
	Type getType() const override { return LIMB_NODE; }
};


struct NullImpl : Object
{
	NullImpl(const Scene& _scene, const IElement& _element)
		: Object(_scene, _element)
	{
	}
	Type getType() const override { return NULL_NODE; }
};


struct NodeAttributeImpl : NodeAttribute
{
	NodeAttributeImpl(const Scene& _scene, const IElement& _element)
		: NodeAttribute(_scene, _element)
	{
	}
	Type getType() const override { return NOTE_ATTRIBUTE; }
	DataView getAttributeType() const override { return attribute_type; }

	
	DataView attribute_type;
};



Cluster::Cluster(const Scene& _scene, const IElement& _element)
	: Object(_scene, _element)
{
}


struct ClusterImpl : Cluster
{
	ClusterImpl(const Scene& _scene, const IElement& _element)
		: Cluster(_scene, _element)
	{
	}

	const int* getIndices() const override { return &indices[0]; }
	int getIndicesCount() const override { return (int)indices.size(); }
	const double* getWeights() const override { return &weights[0]; }
	int getWeightsCount() const override { return (int)weights.size(); }
	Matrix getTransformMatrix() const { return transform_matrix; }
	Matrix getTransformLinkMatrix() const { return transform_link_matrix; }

	std::vector<int> indices;
	std::vector<double> weights;
	Matrix transform_matrix;
	Matrix transform_link_matrix;
	Type getType() const override { return CLUSTER; }
};


struct SkinImpl : Object
{
	SkinImpl(const Scene& _scene, const IElement& _element)
		: Object(_scene, _element)
	{
	}
	Type getType() const override { return SKIN; }
};


struct TextureImpl : Texture
{
	TextureImpl(const Scene& _scene, const IElement& _element)
		: Texture(_scene, _element)
	{
	}


	DataView getName() const override { return name; }
	DataView getFileName() const override { return filename; }


	DataView name;
	DataView filename;
	Type getType() const override { return TEXTURE; }
};


struct Root : Object
{
	Root(const Scene& _scene, const IElement& _element) : Object(_scene, _element) {}
	Type getType() const override { return ROOT; }
};


struct GeometryImpl : Geometry
{
	enum VertexDataMapping
	{
		BY_POLYGON_VERTEX,
		BY_POLYGON
	};

	std::vector<Vec3> vertices;
	std::vector<Vec3> normals;
	VertexDataMapping normals_mapping;
	std::vector<Vec2> uvs;
	VertexDataMapping uvs_mapping;
	std::vector<int> indices;
	std::vector<int> normals_indices;
	std::vector<int> uvs_indices;

	GeometryImpl(const Scene& _scene, const IElement& _element)
		: Geometry(_scene, _element)
	{
	}
	Type getType() const override { return GEOMETRY; }
	int getVertexCount() const override { return (int)vertices.size(); }
	int getIndexCount() const override { return (int)indices.size(); }
	const Vec3* getVertices() const override { return &vertices[0]; }
	const int* getIndices() const override { return &indices[0]; }
	void resolveVertexNormals(Vec3* out) const override;
	void resolveVertexUVs(Vec2* out) const override;
};


struct Scene : IScene
{
	struct Connection
	{
		enum Type {
			OBJECT_OBJECT,
			OBJECT_PROPERTY
		};

		Type type;
		u64 from;
		u64 to;
		DataView property;
	};

	struct ObjectPair
	{
		Element* element;
		Object* object;
	};


	IElement* getRootElement() const override { return m_root_element; }
	Object* getRoot() const override { return m_root; }


	int getObjectCount(Object::Type type) const override
	{
		int count = 0;
		for (const auto& iter : m_object_map)
		{
			if (iter.second.object && iter.second.object->getType() == type)
			{
				++count;
			}
		}
		return count;
	}


	Object* getObject(Object::Type type, int idx) const override
	{
		int counter = idx;
		for (const auto& iter : m_object_map)
		{
			if (iter.second.object && iter.second.object->getType() == type)
			{
				if (counter == 0) return iter.second.object;
				--counter;
			}
		}
		return nullptr;
	}


	void destroy() override { delete this; }


	~Scene()
	{
		for (auto iter : m_object_map)
		{
			delete iter.second.object;
		}
		delete m_root;
		delete m_root_element;
	}


	Element* m_root_element;
	Root* m_root;
	std::unordered_map<u64, ObjectPair> m_object_map;
	std::vector<Connection> m_connections;
	std::vector<u8> m_data;
};


Property* getLastProperty(Element* element)
{
	Property* prop = element->first_property;
	if (!prop) return nullptr;
	while (prop->next) prop = prop->next;
	return prop;
}


Texture* parseTexture(const Scene& scene, Element& element)
{
	TextureImpl* texture = new TextureImpl(scene, element);
	Element* texture_filename = findChild(element, "FileName");
	if (texture_filename && texture_filename->first_property)
	{
		texture->filename = texture_filename->first_property->value;
	}
	Element* texture_name = findChild(element, "TextureName");
	if (texture_name && texture_name->first_property)
	{
		texture->name = texture_name->first_property->value;
	}
	return texture;
}


template <typename T>
T* parse(const Scene& scene, Element& element)
{
	T* obj = new T(scene, element);
	return obj;
}


Object* parseCluster(const Scene& scene, Element& element)
{
	ClusterImpl* obj = new ClusterImpl(scene, element);
	
	Element* indexes = findChild(element, "Indexes");
	if (indexes && indexes->first_property)
	{
		parseBinaryArray(*indexes->first_property, &obj->indices);
	}
	Element* weights = findChild(element, "Weights");
	if (weights && weights->first_property)
	{
		parseBinaryArray(*weights->first_property, &obj->weights);
	}
	Element* transform_link = findChild(element, "TransformLink");
	if (transform_link && transform_link->first_property)
	{
		parseBinaryArrayRaw(*transform_link->first_property, &obj->transform_link_matrix, sizeof(obj->transform_link_matrix));
	}
	Element* transform = findChild(element, "Transform");
	if (transform && transform->first_property)
	{
		parseBinaryArrayRaw(*transform->first_property, &obj->transform_matrix, sizeof(obj->transform_matrix));
	}
	return obj;
}


Object* parseNodeAttribute(const Scene& scene, Element& element)
{
	NodeAttributeImpl* obj = new NodeAttributeImpl(scene, element);
	Element* type_flags = findChild(element, "TypeFlags");
	if (type_flags && type_flags->first_property)
	{
		obj->attribute_type = type_flags->first_property->value;
	}
	return obj;
}


Object* parseLimbNode(const Scene& scene, Element& element)
{

	assert(element.first_property);
	assert(element.first_property->next);
	assert(element.first_property->next->next);
	assert(element.first_property->next->next->value == "LimbNode");

	LimbNodeImpl* obj = new LimbNodeImpl(scene, element);
	return obj;
}


Mesh* parseMesh(const Scene& scene, Element& element)
{
	assert(element.first_property);
	assert(element.first_property->next);
	assert(element.first_property->next->next);
	assert(element.first_property->next->next->value == "Mesh");
	
	MeshImpl* model = new MeshImpl(scene, element);
	model->name = element.first_property->next->value;
	return model;
}


Material* parseMaterial(const Scene& scene, Element& element)
{
	assert(element.first_property);
	MaterialImpl* material = new MaterialImpl(scene, element);
	if (element.first_property && element.first_property->next)
	{
		material->name = element.first_property->next->value;
	}
	Element* prop = findChild(element, "Properties70");
	if (prop) prop = prop->child;
	while(prop)
	{
		if (prop->id == "P" && prop->first_property)
		{
			if (prop->first_property->value == "DiffuseColor")
			{
				// TODO
			}
		}
		prop = prop->sibling;
	}
	return material;
}


u32 getArrayCount(const Property& property)
{
	return *(const u32*)property.value.begin;
}


template <typename T> void parseBinaryArrayRaw(const Property& property, T* out, int max_size)
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
		assert((int)len <= max_size);
		memcpy(out, data, len);
	}
	else if (enc == 1)
	{
		assert(int(elem_size * count) <= max_size);
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

	parseBinaryArrayRaw(property, &(*out)[0], int(sizeof((*out)[0]) * out->size()));
}


template <typename T>
void parseDoubleVecData(Property& property, std::vector<T>* out_vec)
{
	assert(out_vec);
	if (property.type == 'd')
	{
		parseBinaryArray(property, out_vec);
	}
	else
	{
		assert(property.type == 'f');
		assert(sizeof((*out_vec)[0].x) == sizeof(double));
		std::vector<float> tmp;
		parseBinaryArray(property, &tmp);
		int elem_count = sizeof((*out_vec)[0]) / sizeof((*out_vec)[0].x);
		out_vec->resize(tmp.size() / elem_count);
		double* out = &(*out_vec)[0].x;
		for (int i = 0, c = (int)tmp.size(); i < c; ++i)
		{
			out[i] = tmp[i];
		}
	}
}


template <typename T>
static void parseVertexData(Element& element, const char* name, const char* index_name, std::vector<T>* out, std::vector<int>* out_indices, GeometryImpl::VertexDataMapping* mapping)
{
	assert(out);
	assert(mapping);
	Element* data_element = findChild(element, name);
	if (data_element && data_element->first_property)
	{
		Element* mapping_element = findChild(element, "MappingInformationType");
		Element* reference_element = findChild(element, "ReferenceInformationType");

		if (mapping_element && mapping_element->first_property)
		{
			if (mapping_element->first_property->value == "ByPolygonVertex")
			{
				*mapping = GeometryImpl::BY_POLYGON_VERTEX;
			}
			else if (mapping_element->first_property->value == "ByPolygon")
			{
				*mapping = GeometryImpl::BY_POLYGON;
			}
			else
			{
				assert(false);
			}
		}
		if (reference_element && reference_element->first_property)
		{
			if (reference_element->first_property->value == "IndexToDirect")
			{
				Element* indices_element = findChild(element, index_name);
				if (indices_element && indices_element->first_property)
				{
					parseBinaryArray(*indices_element->first_property, out_indices);
				}
			}
			else if (reference_element->first_property->value != "Direct")
			{
				assert(false);
			}
		}
		parseDoubleVecData(*data_element->first_property, out);

	}
}


Geometry* parseGeometry(const Scene& scene, Element& element)
{
	assert(element.first_property);

	Element* vertices_element = findChild(element, "Vertices");
	if (!vertices_element || !vertices_element->first_property) return nullptr;

	Element* polys_element = findChild(element, "PolygonVertexIndex");
	if (!polys_element || !polys_element->first_property) return nullptr;

	GeometryImpl* geom = new GeometryImpl(scene, element);

	parseDoubleVecData(*vertices_element->first_property, &geom->vertices);
	parseBinaryArray(*polys_element->first_property, &geom->indices);

	Element* layer_uv_element = findChild(element, "LayerElementUV");
	if (layer_uv_element)
	{
		parseVertexData(*layer_uv_element, "UV", "UVIndex", &geom->uvs, &geom->uvs_indices, &geom->uvs_mapping);
	}

	Element* layer_normal_element = findChild(element, "LayerElementNormal");
	if (layer_normal_element)
	{
		parseVertexData(*layer_normal_element, "Normals", "NormalsIndex", &geom->normals, &geom->normals_indices, &geom->normals_mapping);
	}

	return geom;
}


void parseConnections(Element& root, Scene* scene)
{
	assert(scene);

	Element* connections = findChild(root, "Connections");
	if (!connections) return;

	Element* connection = connections->child;
	while (connection)
	{
		assert(connection->first_property);
		assert(connection->first_property->next);
		assert(connection->first_property->next->next);

		Scene::Connection c;
		c.from = connection->first_property->next->value.toLong();
		c.to = connection->first_property->next->next->value.toLong();
		if (connection->first_property->value == "OO")
		{
			c.type = Scene::Connection::OBJECT_OBJECT;
		}
		else if (connection->first_property->value == "OP")
		{
			c.type = Scene::Connection::OBJECT_PROPERTY;
			assert(connection->first_property->next->next->next);
			c.property = connection->first_property->next->next->next->value;
		}
		else
		{
			assert(false);
		}
		scene->m_connections.push_back(c);

		connection = connection->sibling;
	}
}


void parseObjects(Element& root, Scene* scene)
{
	Element* objs = findChild(root, "Objects");
	if (!objs) return;

	scene->m_root = new Root(*scene, root);

	Element* object = objs->child;
	while (object)
	{
		u64 uuid = getElementUUID(*object);
		scene->m_object_map[uuid] = {object, nullptr};
		object = object->sibling;
	}

	for (auto iter : scene->m_object_map)
	{
		if (iter.second.element->id == "Geometry")
		{
			Property* last_prop = getLastProperty(iter.second.element);
			if (last_prop && last_prop->value == "Mesh")
			{
				scene->m_object_map[iter.first].object = parseGeometry(*scene, *iter.second.element);
			}
		}
		else if (iter.second.element->id == "Material")
		{
			scene->m_object_map[iter.first].object = parseMaterial(*scene, *iter.second.element);
		}
		else if (iter.second.element->id == "Deformer")
		{
			IElementProperty* class_prop = iter.second.element->getProperty(2);

			if (class_prop)
			{
				if (class_prop->getValue() == "Cluster")
					scene->m_object_map[iter.first].object = parseCluster(*scene, *iter.second.element);
				else if (class_prop->getValue() == "Skin")
					scene->m_object_map[iter.first].object = parse<SkinImpl>(*scene, *iter.second.element);
			}
		}
		else if (iter.second.element->id == "NodeAttribute")
		{
			scene->m_object_map[iter.first].object = parseNodeAttribute(*scene, *iter.second.element);
		}
		else if (iter.second.element->id == "Model")
		{
			IElementProperty* class_prop = iter.second.element->getProperty(2);

			if (class_prop)
			{
				if (class_prop->getValue() == "Mesh")
					scene->m_object_map[iter.first].object = parseMesh(*scene, *iter.second.element);
				else if (class_prop->getValue() == "LimbNode")
					scene->m_object_map[iter.first].object = parseLimbNode(*scene, *iter.second.element);
				else if (class_prop->getValue() == "Null")
					scene->m_object_map[iter.first].object = parse<NullImpl>(*scene, *iter.second.element);
			}
		}
		else if (iter.second.element->id == "Texture")
		{
			scene->m_object_map[iter.first].object = parseTexture(*scene, *iter.second.element);
		}
	}
}



template <typename T>
void resolveVertexData(T* out,
	GeometryImpl::VertexDataMapping mapping,
	const std::vector<T>& data,
	const std::vector<int>& indices)
{
	assert(out);
	assert(!data.empty());
	assert(mapping == GeometryImpl::BY_POLYGON_VERTEX);

	if (indices.empty())
	{
		memcpy(out, &data[0], sizeof(data[0]) * data.size());
	}
	else
	{
		T* cursor = out;
		for (int i = 0, c = (int)indices.size(); i < c; ++i)
		{
			*cursor = data[indices[i]];
			++cursor;
		}
	}

}


void GeometryImpl::resolveVertexNormals(Vec3* out) const
{
	resolveVertexData(out, normals_mapping, normals, normals_indices);
}


void GeometryImpl::resolveVertexUVs(Vec2* out) const
{
	resolveVertexData(out, uvs_mapping, uvs, uvs_indices);
}


IElement* Object::resolveProperty(const char* name)
{
	Element* props = findChild((ofbx::Element&)element, "Properties70");
	if (!props) return nullptr;

	Element* prop = props->child;
	while (prop)
	{
		if (prop->first_property && prop->first_property->value == name)
		{
			return prop;
		}
		prop = prop->sibling;
	}
	return nullptr;
}


Object* Object::resolveObjectLink(Object::Type type) const
{
	u64 id = element.getFirstProperty() ? element.getFirstProperty()->getValue().toLong() : 0;
	for (auto& connection : scene.m_connections)
	{
		if (connection.to == id && connection.from != 0)
		{
			Object* obj = scene.m_object_map.find(connection.from)->second.object;
			if (obj && obj->getType() == type) return obj;
		}
	}
	return nullptr;
}


Object* Object::resolveObjectLink(int idx) const
{
	u64 id = element.getFirstProperty() ? element.getFirstProperty()->getValue().toLong() : 0;
	for (auto& connection : scene.m_connections)
	{
		if (connection.to == id && connection.from != 0)
		{
			Object* obj = scene.m_object_map.find(connection.from)->second.object;
			if (obj)
			{
				if (idx == 0) return obj;
				--idx;
			}
		}
	}
	return nullptr;
}


Object* Object::resolveObjectLink(Object::Type type, const char* property) const
{
	u64 id = element.getFirstProperty() ? element.getFirstProperty()->getValue().toLong() : 0;
	for (auto& connection : scene.m_connections)
	{
		if (connection.to == id && connection.from != 0)
		{
			Object* obj = scene.m_object_map.find(connection.from)->second.object;
			if (obj->getType() == type)
			{
				if(connection.property == property) return obj;
			}
		}
	}
	return nullptr;
}


IScene* load(const u8* data, size_t size)
{
	Scene* scene = new Scene;
	scene->m_data.resize(size);
	memcpy(&scene->m_data[0], data, size);
	Element* root = tokenize(&scene->m_data[0], size);
	if (!root)
	{
		delete scene;
		return nullptr;
	}
	scene->m_root_element = root;
	parseTemplates(*root);
	parseConnections(*root, scene);
	parseObjects(*root, scene);
	return scene;
}


} // namespace ofbx

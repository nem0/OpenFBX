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


Vec3 resolveVec3Property(const Object& object, const char* name, const Vec3& default_value)
{
	Element* element = (Element*)object.resolveProperty(name);
	if (!element) return default_value;
	Property* x = (Property*)element->getProperty(4);
	if (!x || !x->next || !x->next->next) return default_value;

	return {x->value.toDouble(), x->next->value.toDouble(), x->next->next->value.toDouble()};
}


Object::Object(const Scene& _scene, const IElement& _element)
	: scene(_scene)
	, element(_element)
	, is_node(false)
{
	auto& e = (Element&)_element;
	if (e.first_property && e.first_property->next)
	{
		e.first_property->next->value.toString(name);
	}
	else
	{
		name[0] = '\0';
	}
}


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


const Element* findChild(const Element& element, const char* id)
{
	Element* const* iter = &element.child;
	while (*iter)
	{
		if ((*iter)->id == id) return *iter;
		iter = &(*iter)->sibling;
	}
	return nullptr;
}


void parseTemplates(const Element& root)
{
	const Element* defs = findChild(root, "Definitions");
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


Mesh::Mesh(const Scene& _scene, const IElement& _element)
	: Object(_scene, _element)
{}


struct MeshImpl : Mesh
{
	MeshImpl(const Scene& _scene, const IElement& _element)
		: Mesh(_scene, _element)
		, scene(_scene)
	{
		is_node = true;
	}


	Vec3 getGeometricTranslation() const override
	{
		return resolveVec3Property(*this, "GeometricTranslation", {0, 0, 0});
	}


	Vec3 getGeometricRotation() const override { return resolveVec3Property(*this, "GeometricRotation", {0, 0, 0}); }
	Vec3 getGeometricScaling() const override { return resolveVec3Property(*this, "GeometricScaling", {1, 1, 1}); }
	Type getType() const override { return MESH; }
	Matrix evaluateGlobalTransform() const override
	{
		// TODO
		return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	}

	Skin* getSkin() const override
	{
		Geometry* geom = resolveObjectLink<ofbx::Geometry>();
		if (!geom) return nullptr;
		return geom->resolveObjectLink<ofbx::Skin>();
	}


	const Scene& scene;
};


Material::Material(const Scene& _scene, const IElement& _element)
	: Object(_scene, _element)
{}


struct MaterialImpl : Material
{
	MaterialImpl(const Scene& _scene, const IElement& _element)
		: Material(_scene, _element)
	{
	}
	Type getType() const override { return MATERIAL; }
};


struct LimbNodeImpl : Object
{
	LimbNodeImpl(const Scene& _scene, const IElement& _element)
		: Object(_scene, _element)
	{
		is_node = true;
	}
	Type getType() const override { return LIMB_NODE; }
};


struct NullImpl : Object
{
	NullImpl(const Scene& _scene, const IElement& _element)
		: Object(_scene, _element)
	{
		is_node = true;
	}
	Type getType() const override { return NULL_NODE; }
};


NodeAttribute::NodeAttribute(const Scene& _scene, const IElement& _element)
	: Object(_scene, _element)
{}


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


Geometry::Geometry(const Scene& _scene, const IElement& _element)
	: Object(_scene, _element)
{}


struct GeometryImpl : Geometry
{
	enum VertexDataMapping
	{
		BY_POLYGON_VERTEX,
		BY_POLYGON
	};

	std::vector<Vec3> vertices;
	std::vector<Vec3> normals;
	std::vector<Vec2> uvs;

	std::vector<int> to_old_vertices;

	GeometryImpl(const Scene& _scene, const IElement& _element)
		: Geometry(_scene, _element)
	{
	}
	Type getType() const override { return GEOMETRY; }
	int getVertexCount() const override { return (int)vertices.size(); }
	const Vec3* getVertices() const override { return &vertices[0]; }
	int getUVCount() const override { return (int)uvs.size(); }
	int getNormalCount() const override { return (int)normals.size(); }
	const Vec3* getNormals() const override { return &normals[0]; }
	const Vec2* getUVs() const override { return &uvs[0]; }


	void triangulate(std::vector<int>* indices, std::vector<int>* to_old)
	{
		assert(indices);
		assert(to_old);
		std::vector<int> old_indices;
		indices->swap(old_indices);

		auto getIdx = [&old_indices](int i) -> int {
			int idx = old_indices[i];
			return idx < 0 ? -idx - 1 : idx;
		};

		int in_polygon_idx = 0;
		for (int i = 0; i < old_indices.size(); ++i)
		{
			int idx = getIdx(i);
			if (in_polygon_idx <= 2)
			{
				indices->push_back(idx);
				to_old->push_back(i);
			}
			else
			{
				indices->push_back(old_indices[i - in_polygon_idx]);
				to_old->push_back(i - in_polygon_idx);
				indices->push_back(old_indices[i - 1]);
				to_old->push_back(i - 1);
				indices->push_back(idx);
				to_old->push_back(i);
			}
			++in_polygon_idx;
			if (old_indices[i] < 0)
			{
				in_polygon_idx = 0;
			}
		}
	}
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
	virtual int getIndicesCount() const override { return (int)indices.size(); }
	const double* getWeights() const override { return &weights[0]; }
	int getWeightsCount() const override { return (int)weights.size(); }
	Matrix getTransformMatrix() const { return transform_matrix; }
	Matrix getTransformLinkMatrix() const { return transform_link_matrix; }
	Object* getLink() const override { return resolveObjectLink(Object::LIMB_NODE); }
	

	void postprocess()
	{ 
		Object* skin = resolveObjectLinkReverse(Object::SKIN);
		if (!skin) return;

		GeometryImpl* geom = (GeometryImpl*)skin->resolveObjectLinkReverse(Object::GEOMETRY);
		if (!geom) return;

		std::vector<int> old_indices;
		const Element* indexes = findChild((const Element&)element, "Indexes");
		if (indexes && indexes->first_property)
		{
			parseBinaryArray(*indexes->first_property, &old_indices);
		}

		std::vector<double> old_weights;
		const Element* weights_el = findChild((const Element&)element, "Weights");
		if (weights_el && weights_el->first_property)
		{
			parseBinaryArray(*weights_el->first_property, &old_weights);
		}

		assert(old_indices.size() == old_weights.size());
		
		struct NewNode
		{
			int value = -1;
			NewNode* next = nullptr;
		};
		
		struct Pool
		{
			NewNode* pool = nullptr;
			int pool_index = 0;

			Pool(size_t count) { pool = new NewNode[count]; }
			~Pool() { delete[] pool; }

			void add(NewNode& node, int i)
			{
				if (node.value == -1)
				{
					node.value = i;
				}
				else if (node.next)
				{
					add(*node.next, i);
				}
				else
				{
					node.next = &pool[pool_index];
					++pool_index;
					node.next->value = i;
				}
			}
		} pool(geom->to_old_vertices.size());
		
		std::vector<NewNode> to_new;
		
		to_new.resize(geom->to_old_vertices.size());
		for (int i = 0, c = (int)geom->to_old_vertices.size(); i < c; ++i)
		{
			int old = geom->to_old_vertices[i];
			pool.add(to_new[old], i);
		}

		for (int i = 0, c = (int)old_indices.size(); i < c; ++i)
		{
			int old_idx = old_indices[i];
			double w = old_weights[i];
			NewNode* n = &to_new[old_idx];
			while (n)
			{
				indices.push_back(n->value);
				weights.push_back(w);
				n = n->next;
			}
		}
	}


	std::vector<int> indices;
	std::vector<double> weights;
	Matrix transform_matrix;
	Matrix transform_link_matrix;
	Type getType() const override { return CLUSTER; }
};


Skin::Skin(const Scene& _scene, const IElement& _element)
	: Object(_scene, _element)
{}


struct SkinImpl : Skin
{
	SkinImpl(const Scene& _scene, const IElement& _element)
		: Skin(_scene, _element)
	{
	}

	int getClusterCount() const override { return resolveObjectLinkCount(CLUSTER); }
	Cluster* getCluster(int idx) const override { return resolveObjectLink<ofbx::Cluster>(idx); }

	Type getType() const override { return SKIN; }
};


Texture::Texture(const Scene& _scene, const IElement& _element)
	: Object(_scene, _element)
{}


struct TextureImpl : Texture
{
	TextureImpl(const Scene& _scene, const IElement& _element)
		: Texture(_scene, _element)
	{
	}


	DataView getFileName() const override { return filename; }

	DataView filename;
	Type getType() const override { return TEXTURE; }
};


struct Root : Object
{
	Root(const Scene& _scene, const IElement& _element)
		: Object(_scene, _element)
	{
	}
	Type getType() const override { return ROOT; }
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
		const Element* element;
		Object* object;
	};


	IElement* getRootElement() const override { return m_root_element; }
	Object* getRoot() const override { return m_root; }


	int resolveObjectCount(Object::Type type) const override
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


	Object* resolveObject(Object::Type type, int idx) const override
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


Property* getLastProperty(const Element* element)
{
	Property* prop = element->first_property;
	if (!prop) return nullptr;
	while (prop->next) prop = prop->next;
	return prop;
}


Texture* parseTexture(const Scene& scene, const Element& element)
{
	TextureImpl* texture = new TextureImpl(scene, element);
	const Element* texture_filename = findChild(element, "FileName");
	if (texture_filename && texture_filename->first_property)
	{
		texture->filename = texture_filename->first_property->value;
	}
	return texture;
}


template <typename T>
T* parse(const Scene& scene, const Element& element)
{
	T* obj = new T(scene, element);
	return obj;
}


Object* parseCluster(const Scene& scene, const Element& element)
{
	ClusterImpl* obj = new ClusterImpl(scene, element);
	
	const Element* transform_link = findChild(element, "TransformLink");
	if (transform_link && transform_link->first_property)
	{
		parseBinaryArrayRaw(*transform_link->first_property, &obj->transform_link_matrix, sizeof(obj->transform_link_matrix));
	}
	const Element* transform = findChild(element, "Transform");
	if (transform && transform->first_property)
	{
		parseBinaryArrayRaw(*transform->first_property, &obj->transform_matrix, sizeof(obj->transform_matrix));
	}
	return obj;
}


Object* parseNodeAttribute(const Scene& scene, const Element& element)
{
	NodeAttributeImpl* obj = new NodeAttributeImpl(scene, element);
	const Element* type_flags = findChild(element, "TypeFlags");
	if (type_flags && type_flags->first_property)
	{
		obj->attribute_type = type_flags->first_property->value;
	}
	return obj;
}


Object* parseLimbNode(const Scene& scene, const Element& element)
{

	assert(element.first_property);
	assert(element.first_property->next);
	assert(element.first_property->next->next);
	assert(element.first_property->next->next->value == "LimbNode");

	LimbNodeImpl* obj = new LimbNodeImpl(scene, element);
	return obj;
}


Mesh* parseMesh(const Scene& scene, const Element& element)
{
	assert(element.first_property);
	assert(element.first_property->next);
	assert(element.first_property->next->next);
	assert(element.first_property->next->next->value == "Mesh");
	
	return new MeshImpl(scene, element);
}


Material* parseMaterial(const Scene& scene, const Element& element)
{
	assert(element.first_property);
	MaterialImpl* material = new MaterialImpl(scene, element);
	const Element* prop = findChild(element, "Properties70");
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
static void parseVertexData(const Element& element, const char* name, const char* index_name, std::vector<T>* out, std::vector<int>* out_indices, GeometryImpl::VertexDataMapping* mapping)
{
	assert(out);
	assert(mapping);
	const Element* data_element = findChild(element, name);
	if (data_element && data_element->first_property)
	{
		const Element* mapping_element = findChild(element, "MappingInformationType");
		const Element* reference_element = findChild(element, "ReferenceInformationType");

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
				const Element* indices_element = findChild(element, index_name);
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


template <typename T>
void splat(std::vector<T>* out,
	GeometryImpl::VertexDataMapping mapping,
	const std::vector<T>& data,
	const std::vector<int>& indices)
{
	assert(out);
	assert(!data.empty());
	assert(mapping == GeometryImpl::BY_POLYGON_VERTEX);

	if (indices.empty())
	{
		out->resize(data.size());
		memcpy(&(*out)[0], &data[0], sizeof(data[0]) * data.size());
	}
	else
	{
		out->resize(indices.size());
		for (int i = 0, c = (int)indices.size(); i < c; ++i)
		{
			(*out)[i] = data[indices[i]];
		}
	}

}


template <typename T>
static void remap(std::vector<T>* out, std::vector<int> map)
{
	if (out->empty()) return;

	std::vector<T> old;
	old.swap(*out);
	for (int i = 0, c = (int)map.size(); i < c; ++i)
	{
		out->push_back(old[map[i]]);
	}
}


Geometry* parseGeometry(const Scene& scene, const Element& element)
{
	assert(element.first_property);

	const Element* vertices_element = findChild(element, "Vertices");
	if (!vertices_element || !vertices_element->first_property) return nullptr;

	const Element* polys_element = findChild(element, "PolygonVertexIndex");
	if (!polys_element || !polys_element->first_property) return nullptr;

	GeometryImpl* geom = new GeometryImpl(scene, element);

	std::vector<Vec3> vertices;
	parseDoubleVecData(*vertices_element->first_property, &vertices);
	parseBinaryArray(*polys_element->first_property, &geom->to_old_vertices);
	
	std::vector<int> to_old_indices;
	geom->triangulate(&geom->to_old_vertices, &to_old_indices);
	geom->vertices.resize(geom->to_old_vertices.size());

	for (int i = 0, c = (int)geom->to_old_vertices.size(); i < c; ++i)
	{
		geom->vertices[i] = vertices[geom->to_old_vertices[i]];
	}

	const Element* layer_uv_element = findChild(element, "LayerElementUV");
	if (layer_uv_element)
	{
		std::vector<Vec2> tmp;
		std::vector<int> tmp_indices;
		GeometryImpl::VertexDataMapping mapping;
		parseVertexData(*layer_uv_element, "UV", "UVIndex", &tmp, &tmp_indices, &mapping);
		geom->uvs.resize(tmp_indices.empty() ? tmp.size() : tmp_indices.size());
		splat(&geom->uvs, mapping, tmp, tmp_indices);
		remap(&geom->uvs, to_old_indices);
	}

	const Element* layer_normal_element = findChild(element, "LayerElementNormal");
	if (layer_normal_element)
	{
		std::vector<Vec3> tmp;
		std::vector<int> tmp_indices;
		GeometryImpl::VertexDataMapping mapping;
		parseVertexData(*layer_normal_element, "Normals", "NormalsIndex", &tmp, &tmp_indices, &mapping);
		splat(&geom->normals, mapping, tmp, tmp_indices);
		remap(&geom->normals, to_old_indices);
	}

	return geom;
}


void parseConnections(const Element& root, Scene* scene)
{
	assert(scene);

	const Element* connections = findChild(root, "Connections");
	if (!connections) return;

	const Element* connection = connections->child;
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


void parseObjects(const Element& root, Scene* scene)
{
	const Element* objs = findChild(root, "Objects");
	if (!objs) return;

	scene->m_root = new Root(*scene, root);
	scene->m_root->id = 0;

	const Element* object = objs->child;
	while (object)
	{
		u64 uuid = getElementUUID(*object);
		scene->m_object_map[uuid] = {object, nullptr};
		object = object->sibling;
	}

	for (auto iter : scene->m_object_map)
	{
		Object* obj = nullptr;
		if (iter.second.element->id == "Geometry")
		{
			Property* last_prop = getLastProperty(iter.second.element);
			if (last_prop && last_prop->value == "Mesh")
			{
				obj = parseGeometry(*scene, *iter.second.element);
			}
		}
		else if (iter.second.element->id == "Material")
		{
			obj = parseMaterial(*scene, *iter.second.element);
		}
		else if (iter.second.element->id == "Deformer")
		{
			IElementProperty* class_prop = iter.second.element->getProperty(2);

			if (class_prop)
			{
				if (class_prop->getValue() == "Cluster")
					obj = parseCluster(*scene, *iter.second.element);
				else if (class_prop->getValue() == "Skin")
					obj = parse<SkinImpl>(*scene, *iter.second.element);
			}
		}
		else if (iter.second.element->id == "NodeAttribute")
		{
			obj = parseNodeAttribute(*scene, *iter.second.element);
		}
		else if (iter.second.element->id == "Model")
		{
			IElementProperty* class_prop = iter.second.element->getProperty(2);

			if (class_prop)
			{
				if (class_prop->getValue() == "Mesh")
					obj = parseMesh(*scene, *iter.second.element);
				else if (class_prop->getValue() == "LimbNode")
					obj = parseLimbNode(*scene, *iter.second.element);
				else if (class_prop->getValue() == "Null")
					obj = parse<NullImpl>(*scene, *iter.second.element);
			}
		}
		else if (iter.second.element->id == "Texture")
		{
			obj = parseTexture(*scene, *iter.second.element);
		}

		scene->m_object_map[iter.first].object = obj;
		if(obj) obj->id = iter.first;
	}

	for (auto iter : scene->m_object_map)
	{
		Object* obj = iter.second.object;
		if(obj && obj->getType() == Object::CLUSTER)
			((ClusterImpl*)iter.second.object)->postprocess();
	}
}


template <typename T>
int getVertexDataCount(GeometryImpl::VertexDataMapping mapping,
	const std::vector<T>& data,
	const std::vector<int>& indices)
{
	if (data.empty()) return 0;
	assert(mapping == GeometryImpl::BY_POLYGON_VERTEX);

	if (indices.empty())
	{
		return (int)data.size();
	}
	else
	{
		return (int)indices.size();
	}
}


Vec3 Object::getRotationOffset() const
{
	return resolveVec3Property(*this, "RotationOffset", {0, 0, 0});
}


Vec3 Object::getRotationPivot() const
{
	return resolveVec3Property(*this, "RotationPivot", {0, 0, 0});
}


Vec3 Object::getPostRotation() const
{
	return resolveVec3Property(*this, "PostRotation", {0, 0, 0});
}


Vec3 Object::getScalingOffset() const
{
	return resolveVec3Property(*this, "ScalingOffset", {0, 0, 0});
}


Vec3 Object::getScalingPivot() const
{
	return resolveVec3Property(*this, "ScalingPivot", {0, 0, 0});
}


Vec3 Object::getLocalTranslation() const
{
	return resolveVec3Property(*this, "Lcl Translation", { 0, 0, 0 });
}


Vec3 Object::getPreRotation() const
{
	return resolveVec3Property(*this, "PreRotation", { 0, 0, 0 });
}


Vec3 Object::getLocalRotation() const
{
	return resolveVec3Property(*this, "Lcl Rotation", {0, 0, 0});
}


Vec3 Object::getLocalScaling() const
{
	return resolveVec3Property(*this, "Lcl Scaling", {1, 1, 1});
}


IElement* Object::resolveProperty(const char* name) const
{
	const Element* props = findChild((const ofbx::Element&)element, "Properties70");
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


Object* Object::resolveObjectLinkReverse(Object::Type type) const
{
	u64 id = element.getFirstProperty() ? element.getFirstProperty()->getValue().toLong() : 0;
	for (auto& connection : scene.m_connections)
	{
		if (connection.from == id && connection.to != 0)
		{
			Object* obj = scene.m_object_map.find(connection.to)->second.object;
			if (obj && obj->getType() == type) return obj;
		}
	}
	return nullptr;
}


Object* Object::resolveObjectLink(Object::Type type, int idx) const
{
	u64 id = element.getFirstProperty() ? element.getFirstProperty()->getValue().toLong() : 0;
	for (auto& connection : scene.m_connections)
	{
		if (connection.to == id && connection.from != 0)
		{
			Object* obj = scene.m_object_map.find(connection.from)->second.object;
			if (obj && obj->getType() == type)
			{
				if(idx == 0) return obj;
				--idx;
			}
		}
	}
	return nullptr;
}


int Object::resolveObjectLinkCount(Object::Type type) const
{
	int count = 0;
	u64 id = element.getFirstProperty() ? element.getFirstProperty()->getValue().toLong() : 0;
	for (auto& connection : scene.m_connections)
	{
		if (connection.to == id && connection.from != 0)
		{
			Object* obj = scene.m_object_map.find(connection.from)->second.object;
			if (obj && obj->getType() == type) ++count;
		}
	}
	return count;
}


int Object::resolveObjectLinkCount() const
{
	int count = 0;
	u64 id = element.getFirstProperty() ? element.getFirstProperty()->getValue().toLong() : 0;
	for (auto& connection : scene.m_connections)
	{
		if (connection.to == id && connection.from != 0)
		{
			Object* obj = scene.m_object_map.find(connection.from)->second.object;
			if (obj)
			{
				++count;
			}
		}
	}
	return count;
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


Object* Object::getParent() const
{
	Object* parent = nullptr;
	u64 id = element.getFirstProperty() ? element.getFirstProperty()->getValue().toLong() : 0;
	for (auto& connection : scene.m_connections)
	{
		if (connection.from == id && connection.to != 0)
		{
			Object* obj = scene.m_object_map.find(connection.to)->second.object;
			if (obj && obj->is_node)
			{
				assert(parent == nullptr);
				parent = obj;
			}
		}
	}
	return parent;
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

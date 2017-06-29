#pragma once


namespace ofbx
{


typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;

static_assert(sizeof(u8) == 1, "u8 is not 1 byte");
static_assert(sizeof(u32) == 4, "u32 is not 4 bytes");
static_assert(sizeof(u64) == 8, "u64 is not 8 bytes");


struct Vec2
{
	double x, y;
};


struct Vec3
{
	double x, y, z;
};


struct Vec4
{
	double x, y, z, w;
};


struct Matrix
{
	double m[16]; // last 4 are translation
};


struct Quat
{
	double x, y, z, w;
};


struct DataView
{
	const u8* begin;
	const u8* end;

	bool operator!=(const char* rhs) const { return !(*this == rhs); }
	bool operator==(const char* rhs) const;

	u64 toLong() const;
	double toDouble() const;
	
	template <int N>
	void toString(char(&out)[N])
	{
		char* cout = out;
		const u8* cin = begin;
		while (cin != end && cout - out < N - 1)
		{
			*cout = (char)*cin;
			++cin;
			++cout;
		}
		*cout = '\0';
	}
};


struct IElementProperty
{
	enum Type : unsigned char
	{
		LONG = 'L',
		INTEGER = 'I',
		STRING = 'S',
		FLOAT = 'F',
		DOUBLE = 'D',
		ARRAY_DOUBLE = 'd',
		ARRAY_INT = 'i',
	};
	virtual ~IElementProperty() {}
	virtual Type getType() const = 0;
	virtual IElementProperty* getNext() const = 0;
	virtual DataView getValue() const = 0;
	virtual int getCount() const = 0;
	virtual void getValues(double* values, int max_size) const = 0;
	virtual void getValues(int* values, int max_size) const = 0;
};


struct IElement
{
	virtual IElement* getFirstChild() const = 0;
	virtual IElement* getSibling() const = 0;
	virtual DataView getID() const = 0;
	virtual IElementProperty* getFirstProperty() const = 0;
};


struct Scene;


struct Object
{
	enum Type
	{
		ROOT,
		GEOMETRY,
		MATERIAL,
		MESH,
		TEXTURE,
		LIMB_NODE,
		NULL_NODE,
		NOTE_ATTRIBUTE,
		CLUSTER,
		SKIN
	};

	Object(const Scene& _scene, const IElement& _element);

	virtual ~Object() {}
	virtual Type getType() const = 0;
	
	int resolveObjectLinkCount() const;
	int resolveObjectLinkCount(Type type) const;
	Object* resolveObjectLink(int idx) const;
	Object* resolveObjectLink(Type type) const;
	Object* resolveObjectLinkReverse(Type type) const;
	Object* resolveObjectLink(Type type, int idx) const;
	Object* resolveObjectLink(Type type, const char* property) const;
	Object* getParent() const;

	template <typename T> T* resolveObjectLink() const { return static_cast<T*>(resolveObjectLink(T::s_type)); }
	template <typename T> T* resolveObjectLink(int idx) const { return static_cast<T*>(resolveObjectLink(T::s_type, idx)); }

	char name[128];
	
protected:
	IElement* resolveProperty(const char* name) const;
	const Scene& scene;
	const IElement& element;
	bool is_node;
};


struct Material : Object
{
	static const Type s_type = MATERIAL;

	Material(const Scene& _scene, const IElement& _element);
};


struct Cluster : Object
{
	static const Type s_type = CLUSTER;

	Cluster(const Scene& _scene, const IElement& _element);

	virtual const int* getIndices() const = 0;
	virtual int getIndicesCount() const = 0;
	virtual const double* getWeights() const = 0;
	virtual int getWeightsCount() const = 0;
	virtual Matrix getTransformMatrix() const = 0;
	virtual Matrix getTransformLinkMatrix() const = 0;
	virtual Object* getLink() const = 0;
};


struct Skin : Object
{
	static const Type s_type = SKIN;

	Skin(const Scene& _scene, const IElement& _element);

	virtual int getClusterCount() const = 0;
	virtual Cluster* getCluster(int idx) const = 0;
};


struct NodeAttribute : Object
{
	static const Type s_type = NOTE_ATTRIBUTE;

	NodeAttribute(const Scene& _scene, const IElement& _element);

	virtual DataView getAttributeType() const = 0;
};


struct Texture : Object
{
	static const Type s_type = TEXTURE;

	Texture(const Scene& _scene, const IElement& _element);
	virtual DataView getFileName() const = 0;
};


struct Geometry : Object
{
	static const Type s_type = GEOMETRY;

	Geometry(const Scene& _scene, const IElement& _element);

	virtual const Vec3* getVertices() const = 0;
	virtual const int* getIndices() const = 0;
	virtual int getVertexCount() const = 0;
	virtual int getIndexCount() const = 0;

	virtual int getUVCount() const = 0;
	virtual int getNormalCount() const = 0;
	virtual const Vec3* getNormals() const = 0;
	virtual const Vec2* getUVs() const = 0;
};


struct Mesh : Object
{
	static const Type s_type = MESH;

	Mesh(const Scene& _scene, const IElement& _element);

	virtual Vec3 getGeometricTranslation() const = 0;
	virtual Vec3 getGeometricRotation() const = 0;
	virtual Vec3 getGeometricScaling() const = 0;
	virtual Matrix evaluateGlobalTransform() const = 0;
	virtual Skin* getSkin() const = 0;
};


struct IScene
{
	virtual void destroy() = 0;
	virtual IElement* getRootElement() const = 0;
	virtual Object* getRoot() const = 0;
	virtual int resolveObjectCount(Object::Type type) const = 0;
	virtual Object* resolveObject(Object::Type type, int idx) const = 0;
	virtual ~IScene() {}
};


IScene* load(const u8* data, size_t size);


} // namespace ofbx
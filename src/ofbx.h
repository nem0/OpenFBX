#pragma once


namespace ofbx
{


typedef unsigned char u8;
typedef unsigned short u16;
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


struct Color
{
	float r, g, b;
};


struct DataView
{
	const u8* begin = nullptr;
	const u8* end = nullptr;
	bool is_binary = true;

	bool operator!=(const char* rhs) const { return !(*this == rhs); }
	bool operator==(const char* rhs) const;

	u64 toU64() const;
	int toInt() const;
	u32 toU32() const;
	double toDouble() const;
	float toFloat() const;
	
	template <int N>
	void toString(char(&out)[N]) const
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
		ARRAY_LONG = 'l',
		ARRAY_FLOAT = 'f'
	};
	virtual ~IElementProperty() {}
	virtual Type getType() const = 0;
	virtual IElementProperty* getNext() const = 0;
	virtual DataView getValue() const = 0;
	virtual int getCount() const = 0;
	virtual bool getValues(double* values, int max_size) const = 0;
	virtual bool getValues(int* values, int max_size) const = 0;
	virtual bool getValues(float* values, int max_size) const = 0;
	virtual bool getValues(u64* values, int max_size) const = 0;
};


struct IElement
{
	virtual IElement* getFirstChild() const = 0;
	virtual IElement* getSibling() const = 0;
	virtual DataView getID() const = 0;
	virtual IElementProperty* getFirstProperty() const = 0;
};


enum class RotationOrder {
	EULER_XYZ,
	EULER_XZY,
	EULER_YZX,
	EULER_YXZ,
	EULER_ZXY,
	EULER_ZYX,
    SPHERIC_XYZ // Currently unsupported. Treated as EULER_XYZ.
};


struct AnimationCurveNode;
struct AnimationLayer;
struct Scene;
struct IScene;


struct Object
{
	enum class Type
	{
		ROOT,
		GEOMETRY,
		MATERIAL,
		MESH,
		TEXTURE,
		LIMB_NODE,
		NULL_NODE,
		NODE_ATTRIBUTE,
		CLUSTER,
		SKIN,
		ANIMATION_STACK,
		ANIMATION_LAYER,
		ANIMATION_CURVE,
		ANIMATION_CURVE_NODE
	};

	Object(const Scene& _scene, const IElement& _element);

	virtual ~Object() {}
	virtual Type getType() const = 0;
	
	const IScene& getScene() const;
	Object* resolveObjectLink(int idx) const;
	Object* resolveObjectLink(Type type, const char* property, int idx) const;
	Object* resolveObjectLinkReverse(Type type) const;
	Object* getParent() const;

    RotationOrder getRotationOrder() const;
	Vec3 getRotationOffset() const;
	Vec3 getRotationPivot() const;
	Vec3 getPostRotation() const;
	Vec3 getScalingOffset() const;
	Vec3 getScalingPivot() const;
	Vec3 getPreRotation() const;
	Vec3 getLocalTranslation() const;
	Vec3 getLocalRotation() const;
	Vec3 getLocalScaling() const;
	Matrix getGlobalTransform() const;
	Matrix evalLocal(const Vec3& translation, const Vec3& rotation) const;
	bool isNode() const { return is_node; }


	template <typename T> T* resolveObjectLink(int idx) const
	{
		return static_cast<T*>(resolveObjectLink(T::s_type, nullptr, idx));
	}

	u64 id;
	char name[128];
	const IElement& element;
	const Object* node_attribute;

protected:
	bool is_node;
	const Scene& scene;
};


struct Texture : Object
{
	enum TextureType
	{
		DIFFUSE,
		NORMAL,

		COUNT
	};

	static const Type s_type = Type::TEXTURE;

	Texture(const Scene& _scene, const IElement& _element);
	virtual DataView getFileName() const = 0;
	virtual DataView getRelativeFileName() const = 0;
};


struct Material : Object
{
	static const Type s_type = Type::MATERIAL;

	Material(const Scene& _scene, const IElement& _element);

	virtual Color getDiffuseColor() const = 0;
	virtual const Texture* getTexture(Texture::TextureType type) const = 0;
};


struct Cluster : Object
{
	static const Type s_type = Type::CLUSTER;

	Cluster(const Scene& _scene, const IElement& _element);

	virtual const int* getIndices() const = 0;
	virtual int getIndicesCount() const = 0;
	virtual const double* getWeights() const = 0;
	virtual int getWeightsCount() const = 0;
	virtual Matrix getTransformMatrix() const = 0;
	virtual Matrix getTransformLinkMatrix() const = 0;
	virtual const Object* getLink() const = 0;
};


struct Skin : Object
{
	static const Type s_type = Type::SKIN;

	Skin(const Scene& _scene, const IElement& _element);

	virtual int getClusterCount() const = 0;
	virtual const Cluster* getCluster(int idx) const = 0;
};


struct NodeAttribute : Object
{
	static const Type s_type = Type::NODE_ATTRIBUTE;

	NodeAttribute(const Scene& _scene, const IElement& _element);

	virtual DataView getAttributeType() const = 0;
};


struct Geometry : Object
{
	static const Type s_type = Type::GEOMETRY;
    static const int s_uvs_max = 4;

	Geometry(const Scene& _scene, const IElement& _element);

	virtual const Vec3* getVertices() const = 0;
	virtual int getVertexCount() const = 0;

	virtual const Vec3* getNormals() const = 0;
	virtual const Vec2* getUVs(int index = 0) const = 0;
	virtual const Vec4* getColors() const = 0;
	virtual const Vec3* getTangents() const = 0;
	virtual const Skin* getSkin() const = 0;
	virtual const int* getMaterials() const = 0;
};


struct Mesh : Object
{
	static const Type s_type = Type::MESH;

	Mesh(const Scene& _scene, const IElement& _element);

	virtual const Geometry* getGeometry() const = 0;
	virtual Matrix getGeometricMatrix() const = 0;
	virtual const Material* getMaterial(int idx) const = 0;
	virtual int getMaterialCount() const = 0;
};


struct AnimationStack : Object
{
	static const Type s_type = Type::ANIMATION_STACK;

	AnimationStack(const Scene& _scene, const IElement& _element);
	virtual const AnimationLayer* getLayer(int index) const = 0;
};


struct AnimationLayer : Object
{
	static const Type s_type = Type::ANIMATION_LAYER;

	AnimationLayer(const Scene& _scene, const IElement& _element);

	virtual const AnimationCurveNode* getCurveNode(int index) const = 0;
	virtual const AnimationCurveNode* getCurveNode(const Object& bone, const char* property) const = 0;
};


struct AnimationCurve : Object
{
	static const Type s_type = Type::ANIMATION_CURVE;

	AnimationCurve(const Scene& _scene, const IElement& _element);

	virtual int getKeyCount() const = 0;
	virtual const u64* getKeyTime() const = 0;
	virtual const float* getKeyValue() const = 0;
};


struct AnimationCurveNode : Object
{
	static const Type s_type = Type::ANIMATION_CURVE_NODE;

	AnimationCurveNode(const Scene& _scene, const IElement& _element);

	virtual Vec3 getNodeLocalTransform(double time) const = 0;
	virtual const Object* getBone() const = 0;
};


struct TakeInfo
{
	DataView name;
	DataView filename;
	double local_time_from;
	double local_time_to;
	double reference_time_from;
	double reference_time_to;
};


struct IScene
{
	virtual void destroy() = 0;
	virtual const IElement* getRootElement() const = 0;
	virtual const Object* getRoot() const = 0;
	virtual const TakeInfo* getTakeInfo(const char* name) const = 0;
	virtual int getMeshCount() const = 0;
	virtual float getSceneFrameRate() const = 0;
	virtual const Mesh* getMesh(int index) const = 0;
	virtual int getAnimationStackCount() const = 0;
	virtual const AnimationStack* getAnimationStack(int index) const = 0;
	virtual const Object *const * getAllObjects() const = 0;
	virtual int getAllObjectCount() const = 0;

protected:
	virtual ~IScene() {}
};


IScene* load(const u8* data, int size);
const char* getError();


} // namespace ofbx
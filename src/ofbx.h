#pragma once


namespace ofbx
{


typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;


struct DataView
{
	const u8* begin;
	const u8* end;

	bool operator!=(const char* rhs) const { return !(*this == rhs); }

	bool operator==(const char* rhs) const
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
};


struct IObject
{
	enum Type
	{
		MESH
	};

	virtual ~IObject() {}
	virtual Type getType() const = 0;
};


struct IProperty
{
	enum Type : unsigned char
	{
		INTEGER = 'I',
		STRING = 'S',
		FLOAT = 'F',
		DOUBLE = 'D',
		ARRAY_DOUBLE = 'd',
		ARRAY_INT = 'i',
	};
	virtual ~IProperty() {}
	virtual Type getType() const = 0;
	virtual IProperty* getNext() const = 0;
	virtual DataView getValue() const = 0;
	virtual int getCount() const = 0;
	virtual void getValues(double* values) const = 0;
	virtual void getValues(int* values) const = 0;
};


struct INode
{
	virtual INode* getFirstChild() const = 0;
	virtual INode* getSibling() const = 0;
	virtual DataView getID() const = 0;
	virtual IProperty* getFirstProperty() const = 0;
};


struct IScene
{
	virtual bool saveAsOBJ(const char* path) const = 0;
	virtual void destroy() = 0;
	virtual INode* getRoot() const = 0;
	virtual ~IScene() {}
};


IScene* load(const u8* data, size_t size);


} // namespace ofbx
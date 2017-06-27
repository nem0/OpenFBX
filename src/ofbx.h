#pragma once

namespace ofbx
{


typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;


struct IObject
{
	enum Type
	{
		MESH
	};

	virtual ~IObject() {}
	virtual Type getType() const = 0;
};


struct IScene
{
	virtual bool saveAsOBJ(const char* path) const = 0;
	virtual void destroy() = 0;
	virtual ~IScene() {}
};


IScene* load(const u8* data, size_t size);


} // namespace ofbx
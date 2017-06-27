#include "ofbx.h"
#include <cstdio>
#include <memory>

int main()
{
	FILE* fp = fopen("a.fbx", "rb");

	fseek(fp, 0, SEEK_END);
	long file_size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	auto* content = new ofbx::u8[file_size];
	fread(content, 1, file_size, fp);

	ofbx::IScene* scene = ofbx::load((ofbx::u8*)content, file_size);
	scene->saveAsOBJ("out.obj");
	scene->destroy();

	delete[] content;

	fclose(fp);
	return 0;
}
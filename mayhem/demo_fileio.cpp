#include "SampleInterfaces.h"
#include "demo_fileio.h"

#include <cstdio>
#include <string>

namespace
{
std::string g_name;
const uint8_t* g_data = nullptr;
size_t g_size = 0;

// What a gset 'f mesh.obj' row loads: one triangle.
const char kMeshObj[] = "v 0 0 0\nv 10 0 0\nv 0 0 10\nf 1 3 2\n";
const char kMeshName[] = "mesh.obj";
}

void demoFileSetInput(const char* name, const uint8_t* data, size_t size)
{
	g_name = name;
	g_data = data;
	g_size = size;
}

FileIO::~FileIO()
{
	if (fp)
	{
		fclose(fp);
	}
}

bool FileIO::openForWrite(const char*)
{
	return false;
}

// Same as upstream's fopen(path, "rb"), except the only readable files are the registered input and "mesh.obj".
bool FileIO::openForRead(const char* path)
{
	if (fp)
	{
		return false;
	}
	const void* data = nullptr;
	size_t size = 0;
	if (g_data && g_size > 0 && g_name == path)
	{
		data = g_data;
		size = g_size;
		g_name.clear();  // readable once: an 'f' row naming the input itself must not re-read it as an OBJ
	}
	else if (std::string(path) == kMeshName)
	{
		data = kMeshObj;
		size = sizeof(kMeshObj) - 1;
	}
	else
	{
		return false;
	}
	fp = fmemopen(const_cast<void*>(data), size, "rb");
	if (!fp)
	{
		return false;
	}
	mode = Mode::reading;
	return true;
}

bool FileIO::isWriting() const
{
	return mode == Mode::writing;
}

bool FileIO::isReading() const
{
	return mode == Mode::reading;
}

bool FileIO::write(const void*, const size_t)
{
	return false;
}

bool FileIO::read(void* ptr, const size_t size)
{
	if (!fp || mode != Mode::reading)
	{
		return false;
	}
	size_t readLen = fread(ptr, size, 1, fp);
	return readLen == 1;
}

size_t FileIO::getFileSize() const
{
	if (!fp || mode != Mode::reading)
	{
		return false;
	}
	const size_t currentPos = ftell(fp);
	if (fseek(fp, 0, SEEK_END) != 0)
	{
		return 0;
	}
	const size_t size = ftell(fp);
	if (fseek(fp, 0, static_cast<int>(currentPos)) != 0)
	{
		return 0;
	}
	return size;
}

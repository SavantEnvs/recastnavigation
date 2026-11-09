// In-memory stand-in for RecastDemo's FileIO (Source/SampleInterfaces.cpp, which needs SDL/OpenGL).
// Readable: the name the harness registers with demoFileSetInput() (once), and "mesh.obj" (a built-in triangle). Nothing touches
// the filesystem, so every input's result depends on that input alone.
#pragma once
#include <cstddef>
#include <cstdint>

void demoFileSetInput(const char* name, const uint8_t* data, size_t size);

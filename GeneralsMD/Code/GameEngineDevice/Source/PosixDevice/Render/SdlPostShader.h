// Native SDL bindings for the exact shared post HLSL. No shader arithmetic lives here.
#pragma once
#include "postshader.h"
#include <string>

bool Sdl_Post_Vertex_Source(std::string &hlsl);
bool Sdl_Post_Pixel_Source(PostShaderProgram program, std::string &hlsl);

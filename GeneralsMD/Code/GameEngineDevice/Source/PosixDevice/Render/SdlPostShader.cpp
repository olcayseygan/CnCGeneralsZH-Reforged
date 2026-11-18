#include "SdlPostShader.h"
#include "sdl3target.h"

namespace {
void rename_interface(std::string &hlsl, const char *name)
{
    const std::string from = "VertexOutput";
    size_t at = 0;
    while ((at = hlsl.find(from, at)) != std::string::npos) {
        hlsl.replace(at, from.size(), name);
        at += std::string(name).size();
    }
}
}

bool Sdl_Post_Vertex_Source(std::string &hlsl)
{
    hlsl = PostShader_Vertex_Source();
    rename_interface(hlsl, "Output");
    // SDL's Vulkan viewport already flips Y; preserve the original top-left UV arithmetic.
    return SDL3_Shader_Retarget(hlsl, true);
}

bool Sdl_Post_Pixel_Source(PostShaderProgram program, std::string &hlsl)
{
    switch (program) {
    case POST_SHADER_COPY: case POST_SHADER_TONE_MAP: case POST_SHADER_TONE_MAP_WIDE:
    case POST_SHADER_FXAA: case POST_SHADER_SHARPEN: case POST_SHADER_SHARPEN_MULTISAMPLED:
    case POST_SHADER_BLOOM_EXTRACT: case POST_SHADER_BLOOM_BLUR:
    case POST_SHADER_BLOOM_COMPOSITE: case POST_SHADER_BLOOM_COMPOSITE_WIDE:
    case POST_SHADER_AO: case POST_SHADER_AO_SCENE: case POST_SHADER_WORLD_AO:
    case POST_SHADER_WORLD_AO_BLUR: case POST_SHADER_WORLD_AO_UPSAMPLE:
    case POST_SHADER_HAZE: case POST_SHADER_SKY: case POST_SHADER_ATMOSPHERE:
        break;
    default: hlsl.clear(); return false;
    }
    hlsl = PostShader_Pixel_Source(program);
    rename_interface(hlsl, "Input");
    // This pairs Extra's samples with s1 and places textures/samplers in space2 and b0 in
    // space3. Optimized-out declarations are counted by SDL3_Shader_Slots, not guessed here.
    return SDL3_Shader_Retarget(hlsl, false);
}

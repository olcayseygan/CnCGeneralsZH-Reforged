// Exercise WW3D's real sorting pool, including merging adjacent nodes with equal
// material state. Direct device draws cannot catch coverage lost while queued.
#include "PreRTS.h"
#include "Common/GameMemory.h"
#include "Platform/SunShadow.h"
#include "PosixDevice9.h"
#include "dx8wrapper.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "dx8fvf.h"
#include "sortingrenderer.h"
#include "shader.h"
#include "vertmaterial.h"
#include "ffshader.h"
#include "ww3d.h"
#include "wwmath.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <vector>

const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";
static char appPrefix[] = "";
char *gAppPrefix = appPrefix;
RenderWindow ApplicationHWnd = NULL;
Bool ApplicationIsBorderless = FALSE;

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { ++failures; \
    std::printf("FAIL line %d: %s\n", __LINE__, #condition); } } while (0)

static void check_sorted_coverage(PosixDevice9 *device, bool reverse)
{
    CHECK(SunShadow::Begin(device, 64));
    DX8Wrapper::Set_Transform(D3DTS_WORLD, Matrix3D(1));
    DX8Wrapper::Set_Transform(D3DTS_VIEW, Matrix3D(1));
    DX8Wrapper::Set_Projection_Transform_With_Z_Bias(Matrix4x4(true), 0, 1);
    ShaderClass shader;
    shader.Set_Texturing(ShaderClass::TEXTURING_DISABLE);
    shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
    shader.Set_Src_Blend_Func(ShaderClass::SRCBLEND_SRC_ALPHA);
    shader.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA);
    DX8Wrapper::Set_Shader(shader);
    VertexMaterialClass *material = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
    DX8Wrapper::Set_Material(material);
    material->Release_Ref();
    DX8Wrapper::Set_DX8_Render_State(D3DRS_LIGHTING, FALSE);
    DX8Wrapper::Set_DX8_Render_State(D3DRS_ZENABLE, TRUE);
    DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE, D3DFILL_SOLID);
    DX8Wrapper::Set_DX8_Render_State(D3DRS_STENCILENABLE, FALSE);

    SortingVertexBufferClass *vb = new SortingVertexBufferClass(24);
    SortingIndexBufferClass *ib = new SortingIndexBufferClass(24);
    {
        VertexBufferClass::WriteLockClass lock(vb);
        auto *vertices = static_cast<VertexFormatXYZNDUV2 *>(lock.Get_Vertex_Array());
        const unsigned corners[] = {0, 1, 2, 2, 1, 3};
        for (unsigned strip = 0; strip < 4; ++strip) {
            const float left = -1.0f + strip * .5f, right = left + .5f;
            const float x[] = {left, right, left, right};
            const float y[] = {-1, -1, 1, 1};
            for (unsigned v = 0; v < 6; ++v) {
                vertices[strip * 6 + v] = {};
                auto &vertex = vertices[strip * 6 + v];
                vertex.x = x[corners[v]]; vertex.y = y[corners[v]];
                vertex.z = .5f; vertex.nz = 1; vertex.diffuse = 0xffffffff;
            }
        }
    }
    {
        IndexBufferClass::WriteLockClass lock(ib);
        for (unsigned i = 0; i < 24; ++i) lock.Get_Index_Array()[i] = i;
    }
    DX8Wrapper::Set_Vertex_Buffer(vb);
    DX8Wrapper::Set_Index_Buffer(ib, 0);
    // Equal spheres force the same sorting pool; all other draw state is identical.
    // These nodes must not merge across different cloak opacities or into opaque.
    const float opacity[] = {0, .5f, 1, 1};
    const float cleared[4] = {};
    for (unsigned entry = 0; entry < 4; ++entry) {
        const unsigned strip = reverse ? 3 - entry : entry;
        const float parameters[] = {0, 0, 0, opacity[strip]};
        SunShadow::SetCasterEffect(device, strip == 3 ? DRAW_EFFECT_NONE : DRAW_EFFECT_CLOAK_SHADOW, parameters);
        SortingRendererClass::Insert_Triangles(SphereClass(Vector3(0, 0, .5f), 10), strip * 6, 2, strip * 6, 6);
        SunShadow::SetCasterEffect(device, DRAW_EFFECT_NONE, cleared);
    }
    SortingRendererClass::Flush();
    CHECK(SunShadow::GetCasterEffect(device).Effect == DRAW_EFFECT_NONE);
    vb->Release_Ref(); ib->Release_Ref();
    DX8Wrapper::Flush_Deferred_D3D9_State();
    CHECK(SunShadow::End(device));
    std::vector<float> depth;
    CHECK(device->Read_Sun_Shadow(depth));
    if (depth.size() == 64 * 64) {
        // Interior 8x32 rectangles, away from edges, under the cloak's interleaved gradient noise
        // (numpy over these texels: 63 and 129 of 256).
        // Cloak contributes half the opacity: 0%, 25%, 50%; ordinary mesh is 100%.
        const unsigned expected[] = {0, 64, 128, 256};
        for (unsigned strip = 0; strip < 4; ++strip) {
            unsigned occupied = 0;
            for (unsigned y = 16; y < 48; ++y)
                for (unsigned x = strip * 16 + 4; x < strip * 16 + 12; ++x)
                    occupied += depth[y * 64 + x] < 1;
            std::printf("sorted coverage%s, strip %u: %u / 256\n", reverse ? " reversed" : "", strip, occupied);
            CHECK(occupied + 6 >= expected[strip] && occupied <= expected[strip] + 6);
        }
    } else CHECK(false);
    DX8Wrapper::Invalidate_Cached_Render_States();
}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO)) return 77;
    SDL_SetHint("ZH_OFFSCREEN_FRAMES", "1");
    SDL_SetHint("ZH_OFFSCREEN_HZ", "0");
    initMemoryManager();
    WWMath::Init();
    const bool initialized = DX8Wrapper::Init(NULL);
    if (!initialized || WW3D::Set_Render_Device(0, 64, 64, 32, 1, false, false, false) != WW3D_ERROR_OK) {
        std::printf("SKIP: could not create offscreen renderer: %s\n", SDL_GetError());
        if (initialized) DX8Wrapper::Shutdown();
        WWMath::Shutdown();
        SDL_Quit();
        return 77;
    }
    auto *device = static_cast<PosixDevice9 *>(DX8Wrapper::_Get_D3D_Device());
    if (!device || !device->Get_Gpu()) {
        DX8Wrapper::Shutdown();
        WWMath::Shutdown();
        SDL_Quit();
        return 77;
    }
    WW3D::Enable_Sorting(true);
    check_sorted_coverage(device, false);
    check_sorted_coverage(device, true); // Reuses recycled nodes and reverses the final effect.
    SortingRendererClass::Deinit();
    DX8Wrapper::Shutdown();
    WWMath::Shutdown();
    SDL_Quit();
    return failures ? 1 : 0;
}

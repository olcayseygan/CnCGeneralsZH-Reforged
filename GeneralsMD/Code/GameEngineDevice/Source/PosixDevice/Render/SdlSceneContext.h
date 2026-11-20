// Renderer-owned main view snapshot. No camera or deferred caller pointer survives this query.
#pragma once
#include "SdlGpuFrame.h"
enum class SdlSceneView { Main, Reflection, Sun, Other };
struct SdlMainViewSnapshot {
    SdlGpuFrame *Frame = nullptr; // borrowed owner; public immutable recording API only
    SdlTarget Target = {};
    SdlSceneView Pass = SdlSceneView::Other;
    uint64_t DeviceGeneration = 0, FrameSerial = 0, ViewKey = 0;
    float ViewProjection[16] = {}, WorldFromClip[16] = {}; // ORIGINAL camera inverse for explicit-viewport consumers
    float FullTextureWorldFromClip[16] = {}; // full-texture NDC for AO/haze
    float Viewport[6] = {}; int32_t Scissor[4] = {};
    float Near = 1, Far = 1200, ProjectionScale[2] = {};
    SdlSceneCaptureRequest Capture;
    SDL_GPUTexture *SampledDepth = nullptr;
    unsigned DepthWidth = 0, DepthHeight = 0, DepthFormat = 0;
};
// False outside the explicitly entered primary world view, on mirrors/sun/custom views, or without
// a GPU. No capture is acquired by the metadata query, including an empty reach-overlay update.
bool PosixRenderer_Main_View(SdlMainViewSnapshot &snapshot);
// Same owner check plus a fresh/reused independent phase capture. False always empties snapshot.
// Returned texture/buffer resources remain immutable through submission; retire replacements after batch.
bool PosixRenderer_Capture_Main_View(SdlSceneCapturePhase phase, SdlMainViewSnapshot &snapshot);
struct IDirect3DDevice9;
bool PosixRenderer_Main_View(IDirect3DDevice9 *, SdlMainViewSnapshot &snapshot);
bool PosixRenderer_Capture_Main_View(IDirect3DDevice9 *, SdlSceneCapturePhase, SdlMainViewSnapshot &snapshot);

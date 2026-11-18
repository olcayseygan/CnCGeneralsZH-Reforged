// Distance-field reach on the caller's terrain-depth snapshot. No water capture,
// renderer identity or Classic option inference. Main renderer thread only.
#pragma once
#include "SdlGpuFrame.h"
#include <SDL3/SDL.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include "W3DDevice/GameClient/ReachOverlayShaderSources.h"
namespace SdlReachOverlay {
struct Field {
    uint64_t Identity=0,Version=0;uint32_t Colour=0;
    unsigned Width=0,Height=0;float OriginX=0,OriginY=0,Step=0,DistanceLimit=16,Opacity=1;
    // Borrowed only for this call. Metadata is copied; cache-miss upload bytes
    // are copied into owned staging before submission. No CPU pointer is recorded.
    const float *Distance=nullptr,*Hardness=nullptr;size_t DistanceCount=0,HardnessCount=0;
};
struct Look {float BorderWidth,BandWidth,FillAlpha,BandAlpha,LineAlpha,LineWhite,SoftWidth,SoftLine,RippleAlpha,RipplePeriod,RippleSpan,RippleWidth,ShadeWidth,ShadeAlpha;};
Look DefaultLook();
struct Inputs {
    bool Eligible=false;SdlTarget Target={};uint64_t DeviceGeneration=0,FrameSerial=0,ViewKey=0;
    float ViewProjection[16]={},WorldFromClip[16]={},Viewport[6]={};int32_t Scissor[4]={};
    SDL_GPUTexture *TerrainDepth=nullptr;unsigned DepthWidth=0,DepthHeight=0,DepthFormat=0;
    SdlSceneCaptureRequest DepthCapture;float VisualSeconds=0;
};
struct Policies {
    decltype(&SDL_CreateGPUTexture) CreateTexture=SDL_CreateGPUTexture;
    bool (*FlushReaders)(SdlGpuFrame *)=nullptr;
};
struct Statistics {uint64_t TextureAllocations=0,Uploads=0,UploadedBytes=0,RecordedBatches=0,CacheHits=0;uint64_t OwnedTextureBytes=0,PeakOwnedTextureBytes=0;unsigned CachedBatches=0;};
bool Supported(SdlGpuFrame &frame);
// False means no native packet was appended; the caller may draw baked fallback.
// Every resource/batch is preflighted before the atomic immutable packet commit.
bool Draw(SdlGpuFrame &frame,const Inputs &input,const Field *fields,size_t count,const Look &look,Policies policies={});
bool TryRelease();
// Frame teardown callback; foreign/stale owners are harmless no-ops.
bool TryReleaseForFrame(SdlGpuFrame *owner);
Statistics Stats();
// Pure camera helpers follow DX11's depth-range adjustment and scissor coverage.
bool CameraConstants(const Inputs &input,ReachOverlayShaderSources::Constants &constants);
std::array<int32_t,4> Coverage(const Inputs &input,const Field *const *fields,unsigned count);
}

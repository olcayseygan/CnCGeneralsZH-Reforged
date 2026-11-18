#include "SdlReachOverlay.h"
#include "SdlReachOverlayShader.h"
#include "W3DDevice/GameClient/W3DReachOverlay.h"
#include "sdl3shadercompile.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace SdlReachOverlay {
namespace {
using Constants=ReachOverlayShaderSources::Constants;
constexpr unsigned FieldsPerPass=32;
struct Stamp {
    uint64_t Identity,Version;unsigned Width,Height;float OriginX,OriginY,Step;
    bool operator==(const Stamp &b) const {return Identity==b.Identity && Version==b.Version && Width==b.Width && Height==b.Height && OriginX==b.OriginX && OriginY==b.OriginY && Step==b.Step;}
};
struct Key {std::array<Stamp,FieldsPerPass> Fields={};unsigned Count=0;bool operator==(const Key &b) const {if(Count!=b.Count)return false;for(unsigned i=0;i<Count;++i)if(!(Fields[i]==b.Fields[i]))return false;return true;}};
struct Cached {Key Identity;SDL_GPUTexture *Texture=nullptr;uint64_t Bytes=0;bool Used=false;};
struct Ordered {Field Value;size_t Index;};
struct Plan {Constants Values={};SDL_GPUTexture *Texture=nullptr;std::array<int32_t,4> Scissor={};};
struct Pipeline {unsigned Format;SDL_GPUGraphicsPipeline *Native;};
struct State {
    SdlGpuFrame *Frame=nullptr;SDL_GPUDevice *Device=nullptr;uint64_t Generation=0;bool Refused=false;Policies Owner;
    SDL_GPUShader *Vertex=nullptr,*Pixel=nullptr;SDL_GPUSampler *Point=nullptr;SDL_GPUBuffer *Dummy=nullptr;
    std::vector<Pipeline> Pipelines;std::vector<Cached> Cache,Pending;std::vector<Ordered> Order;std::vector<Plan> Plans;
    std::vector<SdlNativeUniformBlock> Blocks;std::vector<SdlNativeDrawPacket> Packets;Statistics Metrics;
};
State state;
bool readers() {return state.Frame && (!state.Frame->Has_Pending_Work() || (state.Owner.FlushReaders ? state.Owner.FlushReaders(state.Frame) : state.Frame->Flush()));}
bool error(const char *) {return false;} // optional provider failure leaves the existing tiled path eligible
bool finite(const float *values,size_t count) {for(size_t i=0;i<count;++i)if(!std::isfinite(values[i]))return false;return true;}
bool nonsingular(const float matrix[16]) {
    double rows[4][4];for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)rows[r][c]=matrix[r*4+c];
    for(unsigned c=0;c<4;++c){unsigned pivot=c;for(unsigned r=c+1;r<4;++r)if(std::fabs(rows[r][c])>std::fabs(rows[pivot][c]))pivot=r;if(std::fabs(rows[pivot][c])<1e-20)return false;if(pivot!=c)for(unsigned k=0;k<4;++k)std::swap(rows[pivot][k],rows[c][k]);for(unsigned r=c+1;r<4;++r){double scale=rows[r][c]/rows[c][c];for(unsigned k=c;k<4;++k)rows[r][k]-=scale*rows[c][k];}}
    return true;
}
void release_pending() {
    for(auto &held:state.Pending) if(held.Texture) {SDL_ReleaseGPUTexture(state.Device,held.Texture);state.Metrics.OwnedTextureBytes-=held.Bytes;}
    state.Pending.clear();
}
struct ClearBorrowed {~ClearBorrowed(){state.Order.clear();state.Plans.clear();state.Blocks.clear();state.Packets.clear();release_pending();}};
bool identity(SdlGpuFrame &frame,const Inputs &in) {
    const auto &capture=in.DepthCapture;const auto &source=capture.Source;
    return in.Eligible && in.Target.Colour && in.Target.Depth && in.Target.Width && in.Target.Height && in.DeviceGeneration
        && in.ViewKey && in.FrameSerial==frame.Scene_Frame() && capture.Frame==in.FrameSerial
        && capture.Generation==frame.Scene_Generation() && capture.ViewKey==in.ViewKey
        && capture.Depth && capture.Phase==SdlSceneCapturePhase::TerrainOverlay
        && in.TerrainDepth && in.TerrainDepth!=in.Target.Colour && in.TerrainDepth!=in.Target.Depth
        && in.DepthFormat==SDL_GPU_TEXTUREFORMAT_R32_FLOAT && in.DepthWidth==in.Target.Width && in.DepthHeight==in.Target.Height
        && source.Colour==in.Target.Colour && source.Depth==in.Target.Depth && source.Width==in.Target.Width && source.Height==in.Target.Height
        && SdlGpuFrame::Target_Colour_Format(source)==SdlGpuFrame::Target_Colour_Format(in.Target)
        && frame.Target_Depth_Format(source)==frame.Target_Depth_Format(in.Target);
}
bool make_shader(bool vertex,SDL_GPUShader *&shader) {
    std::vector<unsigned char> spirv;std::string log;
    if(!SDL3_Compile_HLSL_To_SPIRV(vertex?SdlReachShader::Vertex():SdlReachShader::Pixel(),vertex,spirv,log)
        || !(shader=SDL3_Create_Shader(state.Device,spirv,vertex,log))) return error(log.c_str());
    return true;
}
bool programs() {
    if(state.Refused) return false;
    if(state.Vertex && state.Pixel && state.Point && state.Dummy) return true;
    if(!make_shader(true,state.Vertex) || !make_shader(false,state.Pixel)) {state.Refused=true;return false;}
    SDL_GPUSamplerCreateInfo sampler={};sampler.min_filter=sampler.mag_filter=SDL_GPU_FILTER_NEAREST;sampler.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u=sampler.address_mode_v=sampler.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    state.Point=SDL_CreateGPUSampler(state.Device,&sampler);SDL_GPUBufferCreateInfo buffer={};buffer.size=16;buffer.usage=SDL_GPU_BUFFERUSAGE_VERTEX;
    state.Dummy=SDL_CreateGPUBuffer(state.Device,&buffer);if(!state.Point || !state.Dummy) {state.Refused=true;return error(SDL_GetError());}
    SDL_GPUTransferBufferCreateInfo ti={};ti.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;ti.size=16;auto *transfer=SDL_CreateGPUTransferBuffer(state.Device,&ti);
    if(!transfer) {state.Refused=true;return error(SDL_GetError());}void *bytes=SDL_MapGPUTransferBuffer(state.Device,transfer,false);
    if(!bytes){SDL_ReleaseGPUTransferBuffer(state.Device,transfer);state.Refused=true;return error(SDL_GetError());}memset(bytes,0,16);SDL_UnmapGPUTransferBuffer(state.Device,transfer);
    auto *commands=SDL_AcquireGPUCommandBuffer(state.Device);if(!commands){SDL_ReleaseGPUTransferBuffer(state.Device,transfer);state.Refused=true;return error(SDL_GetError());}
    auto *copy=SDL_BeginGPUCopyPass(commands);if(!copy){SDL_CancelGPUCommandBuffer(commands);SDL_ReleaseGPUTransferBuffer(state.Device,transfer);state.Refused=true;return error(SDL_GetError());}
    SDL_GPUTransferBufferLocation source={transfer,0};SDL_GPUBufferRegion target={state.Dummy,0,16};SDL_UploadToGPUBuffer(copy,&source,&target,false);SDL_EndGPUCopyPass(copy);
    bool ok=SDL_SubmitGPUCommandBuffer(commands);SDL_ReleaseGPUTransferBuffer(state.Device,transfer);if(!ok){state.Refused=true;return error(SDL_GetError());}return true;
}
SDL_GPUGraphicsPipeline *pipeline(unsigned format) {
    for(const auto &p:state.Pipelines)if(p.Format==format)return p.Native;
    SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPUTextureFormat(format);auto &blend=colour.blend_state;
    blend.enable_blend=true;blend.src_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE;blend.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.color_blend_op=blend.alpha_blend_op=SDL_GPU_BLENDOP_ADD;blend.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ZERO;blend.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
    blend.enable_color_write_mask=true;blend.color_write_mask=SDL_GPU_COLORCOMPONENT_R|SDL_GPU_COLORCOMPONENT_G|SDL_GPU_COLORCOMPONENT_B;
    SDL_GPUGraphicsPipelineCreateInfo info={};info.vertex_shader=state.Vertex;info.fragment_shader=state.Pixel;info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;info.rasterizer_state.enable_depth_clip=true;
    info.target_info.num_color_targets=1;info.target_info.color_target_descriptions=&colour;
    auto *native=SDL_CreateGPUGraphicsPipeline(state.Device,&info);if(!native){error(SDL_GetError());return nullptr;}try{state.Pipelines.push_back({format,native});}catch(...){SDL_ReleaseGPUGraphicsPipeline(state.Device,native);throw;}return native;
}
Key key(const Field *const *fields,unsigned count) {
    Key result;result.Count=count;for(unsigned i=0;i<count;++i) {const auto &f=*fields[i];result.Fields[i]={f.Identity,f.Version,f.Width,f.Height,f.OriginX,f.OriginY,f.Step};}return result;
}
Cached *find(const Key &identity) {for(auto &held:state.Cache)if(held.Identity==identity)return &held;for(auto &held:state.Pending)if(held.Identity==identity)return &held;return nullptr;}
bool upload(const Key &identity,const Field *const *fields,unsigned count,Cached &result,const Policies &policies) {
    unsigned width=0,height=0;for(unsigned i=0;i<count;++i){width=(std::max)(width,fields[i]->Width);height=(std::max)(height,fields[i]->Height);}
    const uint64_t bytes=uint64_t(width)*height*count*8;if(!width || !height || bytes>UINT32_MAX)return error("field array extent overflow");
    for(unsigned i=0;i<count;++i){const auto &f=*fields[i];for(size_t n=0;n<size_t(f.Width)*f.Height;++n)if(!std::isfinite(f.Distance[n]) || (f.Hardness && n<f.HardnessCount && !std::isfinite(f.Hardness[n])))return error("non-finite reach grid");}
    SDL_GPUTextureCreateInfo info={};info.type=SDL_GPU_TEXTURETYPE_2D_ARRAY;info.format=SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT;info.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;info.width=width;info.height=height;info.layer_count_or_depth=count;info.num_levels=1;
    auto *texture=policies.CreateTexture(state.Device,&info);if(!texture)return error(SDL_GetError());
    SDL_GPUTransferBufferCreateInfo ti={};ti.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;ti.size=uint32_t(bytes);auto *transfer=SDL_CreateGPUTransferBuffer(state.Device,&ti);
    if(!transfer){SDL_ReleaseGPUTexture(state.Device,texture);return error(SDL_GetError());}
    auto *mapped=static_cast<float *>(SDL_MapGPUTransferBuffer(state.Device,transfer,false));if(!mapped){SDL_ReleaseGPUTransferBuffer(state.Device,transfer);SDL_ReleaseGPUTexture(state.Device,texture);return error(SDL_GetError());}
    // Padding is owned zero data. Original per-field bounds and dimensions ensure
    // the shader only reads each layer's actual field extent.
    memset(mapped,0,size_t(bytes));
    for(unsigned layer=0;layer<count;++layer){const Field &field=*fields[layer];for(unsigned y=0;y<field.Height;++y)for(unsigned x=0;x<field.Width;++x){size_t src=size_t(y)*field.Width+x,dst=(size_t(layer)*width*height+size_t(y)*width+x)*2;mapped[dst]=field.Distance[src];mapped[dst+1]=src<field.HardnessCount && field.Hardness?field.Hardness[src]:1.0f;}}
    SDL_UnmapGPUTransferBuffer(state.Device,transfer);auto *commands=SDL_AcquireGPUCommandBuffer(state.Device);if(!commands){SDL_ReleaseGPUTransferBuffer(state.Device,transfer);SDL_ReleaseGPUTexture(state.Device,texture);return error(SDL_GetError());}
    auto *pass=SDL_BeginGPUCopyPass(commands);if(!pass){SDL_CancelGPUCommandBuffer(commands);SDL_ReleaseGPUTransferBuffer(state.Device,transfer);SDL_ReleaseGPUTexture(state.Device,texture);return error(SDL_GetError());}
    for(unsigned layer=0;layer<count;++layer){SDL_GPUTextureTransferInfo source={};source.transfer_buffer=transfer;source.offset=uint32_t(uint64_t(layer)*width*height*8);source.pixels_per_row=width;source.rows_per_layer=height;SDL_GPUTextureRegion destination={};destination.texture=texture;destination.layer=layer;destination.w=fields[layer]->Width;destination.h=fields[layer]->Height;destination.d=1;SDL_UploadToGPUTexture(pass,&source,&destination,false);}
    SDL_EndGPUCopyPass(pass);bool ok=SDL_SubmitGPUCommandBuffer(commands);SDL_ReleaseGPUTransferBuffer(state.Device,transfer);if(!ok){SDL_ReleaseGPUTexture(state.Device,texture);return error(SDL_GetError());}
    result={identity,texture,bytes,true};++state.Metrics.TextureAllocations;++state.Metrics.Uploads;for(unsigned i=0;i<count;++i)state.Metrics.UploadedBytes+=uint64_t(fields[i]->Width)*fields[i]->Height*8;state.Metrics.OwnedTextureBytes+=bytes;state.Metrics.PeakOwnedTextureBytes=(std::max)(state.Metrics.PeakOwnedTextureBytes,state.Metrics.OwnedTextureBytes);return true;
}
}
Look DefaultLook(){return {ReachLook::BorderWidth,ReachLook::BandWidth,ReachLook::FillAlpha,ReachLook::BandAlpha,ReachLook::LineAlpha,ReachLook::LineWhite,ReachLook::SoftWidth,ReachLook::SoftLine,ReachLook::RippleAlpha,ReachLook::RipplePeriod,ReachLook::RippleSpan,ReachLook::RippleWidth,ReachLook::ShadeWidth,ReachLook::ShadeAlpha};}
bool CameraConstants(const Inputs &in,Constants &out) {
    if(!finite(in.WorldFromClip,16) || !finite(in.ViewProjection,16) || !nonsingular(in.WorldFromClip) || !finite(in.Viewport,6) || in.Viewport[2]<1 || in.Viewport[3]<1 || in.Viewport[4]<0 || in.Viewport[5]>1 || in.Viewport[5]-in.Viewport[4]<=1.0e-8f)return false;
    memcpy(out.WorldFromClip,in.WorldFromClip,sizeof(out.WorldFromClip));float range=in.Viewport[5]-in.Viewport[4];
    for(unsigned column=0;column<4;++column){float row=out.WorldFromClip[8+column]/range;out.WorldFromClip[12+column]-=in.Viewport[4]*row;out.WorldFromClip[8+column]=row;}
    out.Viewport[0]=in.Viewport[0]+.5f;out.Viewport[1]=in.Viewport[1]+.5f;out.Viewport[2]=in.Viewport[2];out.Viewport[3]=in.Viewport[3];return true;
}
std::array<int32_t,4> Coverage(const Inputs &in,const Field *const *fields,unsigned count) {
    int left=(std::max)({0,int(in.Viewport[0]),in.Scissor[0]}),top=(std::max)({0,int(in.Viewport[1]),in.Scissor[1]});
    int right=(std::min)({int(in.Target.Width),int(in.Viewport[0]+in.Viewport[2]),int((std::min)(int64_t(in.Target.Width),int64_t(in.Scissor[0])+in.Scissor[2]))}),bottom=(std::min)({int(in.Target.Height),int(in.Viewport[1]+in.Viewport[3]),int((std::min)(int64_t(in.Target.Height),int64_t(in.Scissor[1])+in.Scissor[3]))});
    std::array<int32_t,4> all={left,top,(std::max)(0,right-left),(std::max)(0,bottom-top)};if(!all[2] || !all[3])return all;float minX=1e30f,minY=1e30f,maxX=-1e30f,maxY=-1e30f;
    for(unsigned f=0;f<count;++f){const Field &field=*fields[f];float xs[]={field.OriginX,field.OriginX+(field.Width-1)*field.Step},ys[]={field.OriginY,field.OriginY+(field.Height-1)*field.Step},zs[]={-20,239.375f};
        for(unsigned corner=0;corner<8;++corner){float point[]={xs[corner&1],ys[(corner>>1)&1],zs[corner>>2],1},clip[4]={};for(unsigned col=0;col<4;++col)for(unsigned row=0;row<4;++row)clip[col]+=point[row]*in.ViewProjection[row*4+col];if(!finite(clip,4) || clip[3]<=.001f)return all;float px=in.Viewport[0]+(clip[0]/clip[3]*.5f+.5f)*in.Viewport[2],py=in.Viewport[1]+(.5f-clip[1]/clip[3]*.5f)*in.Viewport[3];minX=(std::min)(minX,px);maxX=(std::max)(maxX,px);minY=(std::min)(minY,py);maxY=(std::max)(maxY,py);}}
    const int x0=int(std::clamp(std::floor(minX)-1,float(left),float(right))),y0=int(std::clamp(std::floor(minY)-1,float(top),float(bottom)));
    const int x1=int(std::clamp(std::ceil(maxX)+2,float(left),float(right))),y1=int(std::clamp(std::ceil(maxY)+2,float(top),float(bottom)));
    return {x0,y0,(std::max)(0,x1-x0),(std::max)(0,y1-y0)};
}
bool Supported(SdlGpuFrame &frame){auto *d=frame.Device();return d && SDL_GPUTextureSupportsFormat(d,SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT,SDL_GPU_TEXTURETYPE_2D_ARRAY,SDL_GPU_TEXTUREUSAGE_SAMPLER) && SDL_GPUTextureSupportsFormat(d,SDL_GPU_TEXTUREFORMAT_R32_FLOAT,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_SAMPLER);}
bool Draw(SdlGpuFrame &frame,const Inputs &in,const Field *fields,size_t count,const Look &look,Policies policies) {
    if(count && !fields)return false;
    if(!count && (!in.Eligible || !frame.Device()))return true;
    if(!Supported(frame) || !policies.CreateTexture || !in.Eligible || in.DeviceGeneration!=frame.Device_Generation())return false;
    if(!frame.Register_Owner_Release_Hook(TryReleaseForFrame))return false;
    if(state.Device!=frame.Device() || state.Generation!=in.DeviceGeneration || state.Frame!=&frame){if(!TryRelease())return false;state.Frame=&frame;state.Device=frame.Device();state.Generation=in.DeviceGeneration;state.Owner=policies;}
    ClearBorrowed clear;
    try {
        if(!programs())return false;
        auto *native=pipeline(SdlGpuFrame::Target_Colour_Format(in.Target));if(!native)return false;
        if(!count){if(!state.Cache.empty()){if(!readers())return true;for(const auto &held:state.Cache){SDL_ReleaseGPUTexture(state.Device,held.Texture);state.Metrics.OwnedTextureBytes-=held.Bytes;}state.Cache.clear();}state.Metrics.CachedBatches=0;return true;}
        const float lookValues[]={look.BorderWidth,look.BandWidth,look.FillAlpha,look.BandAlpha,look.LineAlpha,look.LineWhite,look.SoftWidth,look.SoftLine,look.RippleAlpha,look.RipplePeriod,look.RippleSpan,look.RippleWidth,look.ShadeWidth,look.ShadeAlpha};
        if(!identity(frame,in) || !std::isfinite(in.VisualSeconds) || !finite(lookValues,14) || look.BandWidth<=0 || look.SoftWidth<=0 || look.RipplePeriod<=0 || look.RippleWidth<=0)return false;
        Constants base={};if(!CameraConstants(in,base))return false;
        state.Order.reserve(count);
        for(size_t i=0;i<count;++i){const auto &f=fields[i];if(f.Opacity<=0)continue;if((!f.Identity && !f.Version) || f.Width<2 || f.Height<2 || !std::isfinite(f.Step) || f.Step<=0 || !std::isfinite(f.OriginX) || !std::isfinite(f.OriginY) || !std::isfinite(f.DistanceLimit) || !std::isfinite(f.Opacity) || uint64_t(f.Width)*f.Height>UINT32_MAX/8 || f.DistanceCount<uint64_t(f.Width)*f.Height || !f.Distance)return false;state.Order.push_back({f,i});}
        std::sort(state.Order.begin(),state.Order.end(),[](const Ordered &a,const Ordered &b){uint32_t x=a.Value.Colour&0xffffff,y=b.Value.Colour&0xffffff;return x==y?a.Index<b.Index:x<y;});
        if(state.Order.empty()){if(!state.Cache.empty()){if(!readers())return true;for(const auto &held:state.Cache){SDL_ReleaseGPUTexture(state.Device,held.Texture);state.Metrics.OwnedTextureBytes-=held.Bytes;}state.Cache.clear();state.Metrics.CachedBatches=0;}return true;}
        size_t batchCount=0;for(size_t at=0;at<state.Order.size();){uint32_t colour=state.Order[at].Value.Colour&0xffffff;unsigned n=0;while(at<state.Order.size() && n<FieldsPerPass && (state.Order[at].Value.Colour&0xffffff)==colour){++at;++n;}++batchCount;}
        if(batchCount>4096)return false;
        state.Plans.reserve(batchCount);state.Pending.reserve(batchCount);state.Blocks.reserve(batchCount);state.Packets.reserve(batchCount);
        for(auto &held:state.Cache)held.Used=false;
        bool changed=false;size_t newBatches=0;
        for(size_t at=0;at<state.Order.size();){const Field *group[FieldsPerPass];uint32_t colour=state.Order[at].Value.Colour&0xffffff;unsigned n=0;while(at<state.Order.size() && n<FieldsPerPass && (state.Order[at].Value.Colour&0xffffff)==colour)group[n++]=&state.Order[at++].Value;auto *held=find(key(group,n));if(held)held->Used=true;else {changed=true;++newBatches;}}
        for(const auto &held:state.Cache)changed|=!held.Used;
        if(newBatches) state.Cache.reserve(state.Cache.size()+newBatches);
        // No persistent resource is replaced while a CPU packet can still read it.
        if(changed && !readers())return false;
        base.Look0[0]=look.BorderWidth;base.Look0[1]=look.BandWidth;base.Look0[2]=look.FillAlpha;base.Look0[3]=look.BandAlpha;
        base.Look1[0]=look.LineAlpha;base.Look1[1]=look.LineWhite;base.Look1[3]=std::fmod(in.VisualSeconds,600.0f);
        base.Look2[0]=look.SoftWidth;base.Look2[1]=look.SoftLine;base.Look2[2]=look.RippleAlpha;base.Look2[3]=look.RipplePeriod;
        base.Look3[0]=look.RippleSpan;base.Look3[1]=look.RippleWidth;base.Look3[2]=look.ShadeWidth;base.Look3[3]=look.ShadeAlpha;
        for(size_t at=0;at<state.Order.size();){const Field *group[FieldsPerPass];uint32_t colour=state.Order[at].Value.Colour&0xffffff;unsigned n=0;while(at<state.Order.size() && n<FieldsPerPass && (state.Order[at].Value.Colour&0xffffff)==colour)group[n++]=&state.Order[at++].Value;
            Key stamp=key(group,n);Cached *held=find(stamp);if(!held){Cached made;if(!upload(stamp,group,n,made,policies))return false;state.Pending.push_back(made);held=&state.Pending.back();}else ++state.Metrics.CacheHits;
            held->Used=true;Plan plan;plan.Values=base;plan.Texture=held->Texture;plan.Scissor=Coverage(in,group,n);plan.Values.Colour[0]=((colour>>16)&255)/255.0f;plan.Values.Colour[1]=((colour>>8)&255)/255.0f;plan.Values.Colour[2]=(colour&255)/255.0f;plan.Values.Look1[2]=float(n);
            for(unsigned i=0;i<n;++i){const auto &f=*group[i];plan.Values.Grid[i][0]=f.OriginX;plan.Values.Grid[i][1]=f.OriginY;plan.Values.Grid[i][2]=1/f.Step;plan.Values.Grid[i][3]=-(std::max)(f.DistanceLimit,1.0f);plan.Values.Size[i][0]=float(f.Width-1);plan.Values.Size[i][1]=float(f.Height-1);plan.Values.Size[i][2]=std::clamp(f.Opacity,0.0f,1.0f);}state.Plans.push_back(plan);
        }
        // Unused arrays are only released after the checked flush above. Failed
        // preflight keeps old cache ownership and releases only fresh arrays.
        if(changed){for(size_t i=state.Cache.size();i-->0;)if(!state.Cache[i].Used){SDL_ReleaseGPUTexture(state.Device,state.Cache[i].Texture);state.Metrics.OwnedTextureBytes-=state.Cache[i].Bytes;state.Cache.erase(state.Cache.begin()+i);}}
        for(auto &held:state.Pending){state.Cache.push_back(held);held.Texture=nullptr;}state.Pending.clear();state.Metrics.CachedBatches=state.Cache.size();
        SdlTarget output=in.Target;output.Depth=nullptr;output.DepthFormat=0;
        for(auto &plan:state.Plans)if(plan.Scissor[2]>0 && plan.Scissor[3]>0){state.Blocks.push_back({1,0,&plan.Values,sizeof(plan.Values)});SdlRecordedDraw draw={};draw.Pipeline=native;draw.VertexBuffer=state.Dummy;draw.Count=3;draw.SamplerCount=2;draw.Textures[0]=in.TerrainDepth;draw.Textures[1]=plan.Texture;draw.Samplers[0]=draw.Samplers[1]=state.Point;memcpy(draw.Viewport,in.Viewport,sizeof(draw.Viewport));memcpy(draw.Scissor,plan.Scissor.data(),sizeof(draw.Scissor));state.Packets.push_back({output,draw,&state.Blocks.back(),1});}
        // The shared atomic API reserves and validates everything before append.
        // A false result can never follow an earlier visible packet from this call.
        if(state.Packets.empty())return true;
        if(!frame.Record_Native_Draw_Batch(state.Packets.data(),state.Packets.size()))return false;
        state.Metrics.RecordedBatches+=state.Packets.size();return true;
    }catch(const std::exception &e){return error(e.what());}
}
bool TryRelease(){if(!state.Device)return true;if(!readers())return false;release_pending();for(auto &held:state.Cache)SDL_ReleaseGPUTexture(state.Device,held.Texture);for(auto &p:state.Pipelines)SDL_ReleaseGPUGraphicsPipeline(state.Device,p.Native);if(state.Vertex)SDL_ReleaseGPUShader(state.Device,state.Vertex);if(state.Pixel)SDL_ReleaseGPUShader(state.Device,state.Pixel);if(state.Point)SDL_ReleaseGPUSampler(state.Device,state.Point);if(state.Dummy)SDL_ReleaseGPUBuffer(state.Device,state.Dummy);state=State();return true;}
bool TryReleaseForFrame(SdlGpuFrame *owner){return state.Frame!=owner || TryRelease();}
Statistics Stats(){return state.Metrics;}
}

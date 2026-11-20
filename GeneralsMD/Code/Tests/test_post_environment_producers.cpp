// Actual native producer decisions and reset epochs, independent of scene/GPU
// readiness. Scene math and borrowed native resources have separate GPU tests.
#include "PreRTS.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "Platform/RenderPost.h"
#include <cstdio>
#include <limits>
static unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(0)
int main()
{
#if defined(__linux__)
    using namespace NativePostEnvironmentPolicy;
    CHECK(chooseHaze(true,false,true,false,false,false)==None);
    CHECK(chooseHaze(true,false,true,true,false,false)==WarmLight);
    CHECK(chooseHaze(true,false,true,false,true,true)==NativePostEnvironmentPolicy::Weather);
    CHECK(chooseHaze(true,false,true,true,true,true)==NativePostEnvironmentPolicy::Weather); // replaces, never stacks
    CHECK(chooseHaze(true,false,true,true,true,false)==WarmLight);
    CHECK(chooseHaze(true,false,true,false,true,false)==None);
    CHECK(chooseHaze(false,false,true,true,true,true)==None);
    CHECK(chooseHaze(true,true,true,true,true,true)==None);
    CHECK(chooseHaze(true,false,false,true,true,true)==None);

    CHECK(atmosphere(true,false,true,true,true,false,false));
    CHECK(!atmosphere(false,false,true,true,true,false,false));
    CHECK(!atmosphere(true,true,true,true,true,false,false));
    CHECK(!atmosphere(true,false,false,true,true,false,false));
    CHECK(!atmosphere(true,false,true,false,true,false,false));
    CHECK(!atmosphere(true,false,true,true,false,false,false));
    CHECK(!atmosphere(true,false,true,true,true,true,false));
    CHECK(!atmosphere(true,false,true,true,true,false,true));
    // WarmLight admission is deliberately independent of any active post scene.
    CHECK(chooseHaze(true,false,true,true,false,true)==WarmLight);

    int map=0,samples=0,otherMap=0,otherSamples=0;
    MapEpoch epoch;CHECK(epoch.Generation!=0);
    CHECK(epoch.observe(&map,&samples,64,48));const auto first=epoch.Generation;
    CHECK(!epoch.observe(&map,&samples,64,48));CHECK(epoch.Generation==first);
    // The next map can reuse every address/dimension. The reset event remains
    // authoritative; pointer/sample/size equality cannot suppress its refresh.
    epoch.invalidate();CHECK(epoch.Map==nullptr && epoch.Samples==nullptr && epoch.Width==0 && epoch.Height==0);
    CHECK(epoch.observe(&map,&samples,64,48));CHECK(epoch.Generation!=first);
    auto previous=epoch.Generation;CHECK(epoch.observe(&otherMap,&samples,64,48));CHECK(epoch.Generation!=previous);
    previous=epoch.Generation;CHECK(epoch.observe(&otherMap,&otherSamples,64,48));CHECK(epoch.Generation!=previous);
    previous=epoch.Generation;CHECK(epoch.observe(&otherMap,&otherSamples,64,49));CHECK(epoch.Generation!=previous);
    CHECK(epoch.observe(nullptr,nullptr,0,0));CHECK(!epoch.Map && !epoch.Samples);
    epoch.Generation=std::numeric_limits<std::uint64_t>::max();epoch.invalidate();CHECK(epoch.Generation==1);

    RenderPost::Environment empty;
    CHECK(!empty.MapIdentity && !empty.MapGeneration && !empty.Sky && !empty.Shroud);
    CHECK(empty.Haze.Strength==0 && empty.Atmosphere.Strength==0);
    CHECK(empty.ShroudRect[0]==0 && empty.ShroudRect[1]==0 && empty.ShroudRect[2]==1 && empty.ShroudRect[3]==1);
    CHECK(empty.Haze.Thickness==1 && empty.Haze.Reach==1 && empty.Haze.WaterHeight==-1.0e6f);
#endif
    std::printf("Native post environment producer policy: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}

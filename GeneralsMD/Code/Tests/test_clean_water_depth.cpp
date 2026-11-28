#include "test_harness.h"
#include "WaterMediumDepthTest.h"
#include "Platform/WaterCleanPrecompute.h"
#include <limits>

TEST(clean_depth_and_shore_geometry) {WaterMediumDepthTest::Check();}

TEST(clean_atlas_matches_quantized_bilinear_depth_and_derivatives) {
    WaterMediumDepth::Field depth;depth.width=7;depth.height=5;
    for(int y=0;y<depth.height;++y)for(int x=0;x<depth.width;++x)
        depth.depth.push_back(WaterMediumDepth::Encode(float(x*x+3*y))*(WaterMediumDepth::Cap/255.f));
    WaterCleanPrecompute::Image image;
    CHECK(WaterCleanPrecompute::BuildDepth(depth,8,image));
    for(float gy:{.1f,1.2f,3.6f,4.5f,6.8f})for(float gx:{.2f,2.4f,5.7f,6.8f}) {
        const int x=int(gx),y=int(gy);const float fx=gx-x,fy=gy-y;
        const auto a=image.pixels[size_t(y)*image.width+x];
        const float value=a.r+a.g*fx+a.b*fy+a.a*fx*fy;
        CHECK_NEAR(value,WaterMediumDepth::Sample(depth,gx,gy),1e-5f);
        const float epsilon=.001f;
        CHECK_NEAR(a.g+a.a*fy,(WaterMediumDepth::Sample(depth,gx+epsilon,gy)-WaterMediumDepth::Sample(depth,gx-epsilon,gy))/(2*epsilon),.003f);
        CHECK_NEAR(a.b+a.a*fx,(WaterMediumDepth::Sample(depth,gx,gy+epsilon)-WaterMediumDepth::Sample(depth,gx,gy-epsilon))/(2*epsilon),.003f);
    }
}

TEST(clean_atlas_accepts_wet_inland_and_heading_off_is_identity) {
    constexpr int w=11,h=9;std::vector<uint8_t> wet(w*h,255),exposure(w*h,0),heights(w*h,0),shore(w*h,255);
    WaterMediumDepth::Field depth;
    CHECK(WaterMediumDepth::Build(heights.data(),wet.data(),shore.data(),w,h,10,.625f,20,depth));
    CHECK(WaterCleanShore::Build(depth,wet.data(),10));
    WaterWaveHeading::Field heading;
    CHECK(WaterWaveHeading::Build(wet.data(),exposure.data(),w,h,0,10,heading));
    WaterCleanPrecompute::Atlas first,second;
    CHECK(WaterCleanPrecompute::BuildAtlas(depth,heading,16,0,10,false,first));
    CHECK(first.image.revision!=0 && first.info[0]==16 && first.info[3]==1);
    CHECK_NEAR(first.origin[2],1.f/first.image.width,1e-8f);
    CHECK_NEAR(first.origin[3],1.f/first.image.height,1e-8f);
    for(int y=0;y<int(first.info[2]);++y)for(int x=0;x<int(first.info[1]);++x) {
        const auto &pixel=first.image.pixels[size_t(y+16)*first.image.width+x];
        CHECK(pixel.r==1 && pixel.g==0 && pixel.b==0 && pixel.a==1);
    }
    // A distinct water elevation creates distinct immutable depth transport and a
    // new resource key; a cache must never inherit the preceding level's atlas.
    for(auto &d:depth.depth)d=2;
    CHECK(WaterCleanPrecompute::BuildAtlas(depth,heading,16,0,10,true,second));
    CHECK(first.image.revision!=second.image.revision);
    CHECK(first.image.pixels[0].r!=second.image.pixels[0].r);
}

TEST(clean_atlas_rejects_empty_partial_and_dry_transport) {
    using namespace WaterCleanPrecompute;
    WaterMediumDepth::Field depth;WaterWaveHeading::Field heading;Atlas atlas;
    CHECK(!BuildAtlas(depth,heading,8,0,10,true,atlas));
    depth.width=depth.height=2;depth.depth={1,2,3};
    CHECK(!BuildAtlas(depth,heading,8,0,10,true,atlas));
    depth.depth.push_back(4);heading.texels.resize(8);
    CHECK(!BuildAtlas(depth,heading,8,0,10,true,atlas)); // absent shore transport
    depth.shoreDistance.assign(4,0);depth.shoreX.assign(4,128);
    depth.shoreY.assign(4,128);depth.shoreConfidence.assign(4,0);
    CHECK(!BuildAtlas(depth,heading,8,0,10,true,atlas)); // complete but dry heading transport
    CHECK(!BuildAtlas(depth,heading,8,0,std::numeric_limits<float>::infinity(),true,atlas));
    CHECK(atlas.image.revision==0);
}

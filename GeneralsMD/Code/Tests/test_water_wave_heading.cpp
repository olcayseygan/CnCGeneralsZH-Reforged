#include "test_harness.h"
#include "WaterWaveHeadingTest.h"
TEST(water_wave_travel_channels_exposure_shelter_and_bounds) {WaterWaveHeadingTest::Check();}
TEST(disconnected_seas_do_not_borrow_exposure_or_phase_origins) {
    using namespace WaterWaveHeading;
    const int w=161,h=81;std::vector<uint8_t> wet(w*h,0),exposure(w*h,0);Field first,second;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)if(x<=60 || x>=100)wet[y*w+x]=255;
    CHECK(Build(wet.data(),exposure.data(),w,h,0,10,first));
    for(int y=0;y<h;++y)for(int x=100;x<w;++x)exposure[y*w+x]=255;
    CHECK(Build(wet.data(),exposure.data(),w,h,0,10,second));
    for(int y=10;y<h-10;++y)for(int x=10;x<50;++x) {
        const size_t i=2*(y*w+x);const Vector a=DecodeGradient(first.texels[i]),b=DecodeGradient(second.texels[i]);
        CHECK(a.x==b.x && a.y==b.y);
        CHECK_NEAR(DecodeU(first.texels[i],first.uStep),DecodeU(second.texels[i],second.uStep),.1f);
        CHECK(first.texels[i+1]==second.texels[i+1]);
    }
}
TEST(polygon_gaps_between_adjacent_wet_cells_block_cross_sea_travel) {
    using namespace WaterWaveHeading;
    const int w=80,h=40;std::vector<uint8_t> wet(w*h,255),exposure(w*h,0),links(w*h,3);Field split,connected;
    for(int y=0;y<h;++y)links[y*w+39]&=~1u;
    CHECK(Build(wet.data(),exposure.data(),w,h,0,10,split,links.data()));
    CHECK(Build(wet.data(),exposure.data(),w,h,0,10,connected));
    // The western sea receives only through its northern edge. Waves cannot cross
    // the uncovered polygon seam from the eastern sea's incoming right edge.
    for(int y=5;y<h-5;++y) {
        const size_t i=2*(y*w+39);const Vector a=DecodeGradient(split.texels[i]),b=DecodeGradient(connected.texels[i]);
        CHECK(std::abs(a.x)<.4f && a.y<-.9f);
        CHECK(b.x<-.6f && b.y<-.6f);
    }
}

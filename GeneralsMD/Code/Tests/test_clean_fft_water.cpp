#include "test_harness.h"
#include "Platform/WaterFftFiltering.h"
#include "Platform/WaterCleanCollapse.h"
#include <limits>

TEST(clean_spectrum_power_reality_determinism_and_variance) {
    using namespace WaterFftSpectrum;
    for(unsigned resolution:{128u,256u}) {
        auto c=ForResolution(resolution);const auto clean=InitialSpectrum(c,true),old=InitialSpectrum(c,false);
        CHECK(clean!=old);CHECK(clean==InitialSpectrum(c,true));CHECK(old==InitialSpectrum(c));
        const auto rms=SpectrumHeightRms(clean);double sum=0,target=0;
        for(unsigned band=0;band<3;++band){CHECK(rms[band]>0);sum+=rms[band]*rms[band];target+=HeightRms[band]*HeightRms[band];}
        CHECK_NEAR(sum,target,1e-5);
        for(unsigned band=0;band<3;++band)for(unsigned y=0;y<resolution;++y)for(unsigned x=0;x<resolution;++x) {
            const auto at=(band*resolution*resolution+y*resolution+x)*4;
            const auto other=(band*resolution*resolution+((resolution-y)%resolution)*resolution+(resolution-x)%resolution)*4;
            CHECK(clean[at+2]==clean[other] && clean[at+3]==-clean[other+1]);
            for(unsigned i=0;i<4;++i)CHECK(std::isfinite(clean[at+i]));
            if(x==0 || y==0)for(unsigned i=0;i<4;++i)CHECK_EQ(clean[at+i],0.f);
        }
        CHECK(WaterFftFiltering::SpectrumVariance(c,true)==WaterFftFiltering::UnresolvedSlopeVariance(clean,c));
    }
    const WaterCleanSpectrum::Parameters p;const double peak=WaterCleanSpectrum::PeakFrequency(p);
    for(double ratio:{.25,.75,1.,2.,8.}) {
        double integral=0;for(int i=0;i<16384;++i)integral+=WaterCleanSpectrum::Spreading(-Pi+(i+.5)*2*Pi/16384,peak*ratio,p)*2*Pi/16384;
        CHECK_NEAR(integral,1.,1e-6);
    }
}
TEST(clean_tile_collapse_rejects_equal_large_turns_and_boundary) {
    using namespace WaterCleanCollapse;
    const float small[3]={.01f,-.005f,.02f},large[3]={.5f,.5f,.5f},boundary[3]={0,AngleRadians,0};
    const float invalid[3]={0,std::numeric_limits<float>::quiet_NaN(),0};
    CHECK(Collapses(small));CHECK(!Collapses(large));CHECK(!Collapses(boundary));CHECK(!Collapses(invalid));
    const float weight[3]={.3f,.6f,.8f},dx[3]={-.01f,.02f,.005f},dy[3]={.003f,-.004f,.008f};
    const auto mean=Evaluate(small,weight,dx,dy);CHECK(mean.collapse);
    const float step=.001f;float plus[3],minus[3];
    for(int axis=0;axis<2;++axis) {
        const float *gradient=axis?dy:dx;
        for(int i=0;i<3;++i){plus[i]=weight[i]+step*gradient[i];minus[i]=weight[i]-step*gradient[i];}
        const float finite=(Evaluate(small,plus,dx,dy).angle-Evaluate(small,minus,dx,dy).angle)/(2*step);
        CHECK_NEAR(finite,axis?mean.dy:mean.dx,2e-6);
    }
    CHECK_NEAR(mean.scale,1.7f,1e-6);CHECK_NEAR(mean.scaleX,.015f,1e-6);CHECK_NEAR(mean.scaleY,.007f,1e-6);
}

#include "test_harness.h"
#include "WaterMediumDepthTest.h"
#include "Platform/WaterFftFiltering.h"
#include <limits>
TEST(clean_depth_and_shore_geometry) {WaterMediumDepthTest::Check();}
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
  }
  CHECK(WaterFftFiltering::SpectrumVariance(c,true)==WaterFftFiltering::UnresolvedSlopeVariance(clean,c));
 }
 const WaterCleanSpectrum::Parameters p;const double peak=WaterCleanSpectrum::PeakFrequency(p);
 for(double ratio:{.25,.75,1.,2.,8.}) {
  double integral=0;for(int i=0;i<16384;++i)integral+=WaterCleanSpectrum::Spreading(-Pi+(i+.5)*2*Pi/16384,peak*ratio,p)*2*Pi/16384;
  CHECK_NEAR(integral,1.,1e-6);
 }
}
TEST(clean_shore_startup_transport_rejects_equal_empty_and_partial_fields) {
 WaterMediumDepth::Field f;const uint32_t neutral=0x00ff0000u;
 CHECK_EQ(WaterCleanShore::DistanceWord(f,0),neutral);CHECK_EQ(WaterCleanShore::Word(f,0),0u);
 CHECK_EQ(WaterCleanShore::DistanceWord(f,19),neutral);
 f.depth={0,10,20};CHECK_EQ(WaterCleanShore::DistanceWord(f,0),neutral);
 f.shoreDistance={0,1500};CHECK_EQ(WaterCleanShore::DistanceWord(f,1),neutral);
 f.shoreDistance.push_back(WaterCleanShore::DistanceCap);
 CHECK_EQ(WaterCleanShore::DistanceWord(f,0),0u);CHECK_EQ(WaterCleanShore::DistanceWord(f,1),0x00800000u);
 CHECK_EQ(WaterCleanShore::DistanceWord(f,2),neutral);CHECK_EQ(WaterCleanShore::DistanceWord(f,3),neutral);
 CHECK_EQ(WaterCleanShore::DistanceWord(f,std::numeric_limits<size_t>::max()),neutral);
}

#include "test_harness.h"
#include "Platform/WaterOpticalModel.h"
#include <limits>
#include <initializer_list>
#include <cstring>
float displayedBed(float bed,float scatter,float transmission)
{
	return std::pow(std::pow(bed,2.2f)*transmission+scatter*(1.0f-transmission),1.0f/2.2f);
}
float displayedBedContrast(float transmission,float scatter)
{
	return (displayedBed(0.65f,scatter,transmission)-displayedBed(0.25f,scatter,transmission))/0.4f;
}
TEST(water_material_clarity_restores_shallow_scenery_without_losing_depth_absorption)
{
    using namespace WaterOpticalModel;
    // SW's original floor is ~1.875 units below Z=5; the angled view has a longer ray.
    // Test material contrast, not just a coefficient: stonework must remain distinguishable.
    const Rgb opaque=Transmission(3.2f,0,1,3,1);
    const Rgb clear=Transmission(3.2f,0,1,3,1,.9f);
    const Rgb source=ScatteringSource(0,1,1,.5f);
    CHECK(displayedBedContrast(clear.g,source.g)>.45f);
    CHECK(displayedBedContrast(clear.g,source.g)>5*displayedBedContrast(opaque.g,source.g));
    for(float clarity:{0.f,.5f,.9f,1.f}) {
        Rgb previous=Transmission(0,0,1,3,1,clarity);
        for(float distance:{.25f,1.f,3.2f,10.f,50.f,500.f}) {
            const Rgb current=Transmission(distance,0,1,3,1,clarity);
            CHECK(current.r<=previous.r && current.g<=previous.g && current.b<=previous.b);
            previous=current;
        }
        CHECK(previous.b<1e-9f);
    }
    const Rgb natural=Transmission(12,0,0,3,1),maximum=Transmission(12,0,1,3,1,1);
    CHECK(memcmp(&natural,&maximum,sizeof(Rgb))==0);
    for(float invalid:{-1.f,std::numeric_limits<float>::quiet_NaN()}) {
        const Rgb unchanged=Transmission(3.2f,0,1,3,1,invalid);
        CHECK(memcmp(&opaque,&unchanged,sizeof(Rgb))==0);
    }
}

TEST(water_clarity_geography_preserves_shelves_but_rejects_flat_offshore_basins)
{
    using namespace WaterOpticalModel;
    // Terrain samples from the two ruins locations: a raised bed surrounded by
    // lower wet terrain. The central shelf is fully eligible; the southern ledge fades.
    CHECK_EQ(ClarityEligibility(1.875f,5.0f,1),1.0f);
    const float southern=ClarityEligibility(3.275f,5.0f,1);
    CHECK(southern>.8f && southern<.9f);
    CHECK_EQ(ClarityEligibility(1.875f,1.875f,1),0.0f); // uniformly shallow open sea
    CHECK_EQ(ClarityEligibility(1.875f,2.375f,1),0.0f); // tiny height noise is not a shelf
    CHECK_EQ(ClarityEligibility(1.875f,1.875f,0),1.0f); // enclosed/nearshore shallow pool
    CHECK_EQ(ClarityEligibility(1.875f,1.875f,.25f),.75f);
    for (float offshore:{0.f,.5f,1.f}) for (float terrainDepth:{ClarityMaximumDepth,6.f,7.f,50.f}) {
        CHECK_EQ(ClarityEligibility(terrainDepth,terrainDepth+.625f,offshore),0.0f);
        CHECK_EQ(ClarityEligibility(terrainDepth,terrainDepth+100.f,offshore),0.0f);
    }
    // A short ray may hit a hull in deep water. Classification still uses the
    // terrain underneath it, leaving authored opacity intact for every hit distance.
    const float basin=ClarityEligibility(6.375f,7.f,1);
    for (float sceneRay:{.1f,.5f,3.2f,10.f}) {
        const Rgb baseline=Transmission(sceneRay,0,1,3,1);
        const Rgb classified=Transmission(sceneRay,0,1,3,1,.9f*basin);
        CHECK(memcmp(&baseline,&classified,sizeof(Rgb))==0);
    }
    const float invalids[]={std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()};
    for (float invalid:invalids) {
        CHECK_EQ(ClarityEligibility(invalid,5,1),0.0f);
        CHECK_EQ(ClarityEligibility(2,invalid,1),0.0f);
        CHECK_EQ(ClarityEligibility(2,5,invalid),0.0f);
    }
}

TEST(water_clarity_geography_blends_continuously_across_depth_relief_and_offshore)
{
    using namespace WaterOpticalModel;
    float previousDepth=1,previousRelief=0,previousOffshore=1;
    for (int i=0;i<=400;++i) {
        const float t=i/400.f;
        const float depth=ClarityEligibility(2.5f+3*t,4.5f+3*t,1);
        const float relief=ClarityEligibility(2,2+.5f+t,1);
        const float offshore=ClarityEligibility(2,2,t);
        CHECK(depth>=0 && depth<=previousDepth && previousDepth-depth<.004f);
        CHECK(relief>=previousRelief && relief<=1 && relief-previousRelief<.004f);
        CHECK(offshore>=0 && offshore<=previousOffshore && previousOffshore-offshore<.004f);
        previousDepth=depth;previousRelief=relief;previousOffshore=offshore;
    }
    CHECK_EQ(previousDepth,0.0f);
    CHECK_EQ(previousRelief,1.0f);
    CHECK_EQ(previousOffshore,0.0f);
    // Moving either input through a threshold cannot introduce a visible step.
    const float epsilon=.0001f;
    for (float depth:{ClarityShallowDepth,ClarityMaximumDepth})
        CHECK_NEAR(ClarityEligibility(depth-epsilon,depth+2,1),ClarityEligibility(depth+epsilon,depth+2,1),.00001f);
    for (float relief:{ClarityReliefStart,ClarityReliefFull})
        CHECK_NEAR(ClarityEligibility(2,2+relief-epsilon,1),ClarityEligibility(2,2+relief+epsilon,1),.00001f);
}

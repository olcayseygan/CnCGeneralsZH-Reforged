#pragma once
#include "Platform/WaterMediumDepth.h"
#include "Platform/WaterCleanShore.h"
#include "Platform/WaterWaveHeading.h"
#include <limits>
// depth transport and shoreline invariants, independent of a render device.
namespace WaterMediumDepthTest {
inline void Check() {
    using namespace WaterMediumDepth;
    const int w=17,h=9;
    std::vector<uint8_t> heights(w*h,0),wet(w*h,255),shore(w*h,255);
    Field field,again;
    CHECK(Build(heights.data(),wet.data(),shore.data(),w,h,10,.625f,20,field));
    for(float d:field.depth) CHECK_NEAR(d,40,1e-5);
    // Dry samples with deliberately deep shore bytes must not lower adjacent wet water.
    for(int y=0;y<h;++y)wet[y*w]=0;
    CHECK(Build(heights.data(),wet.data(),shore.data(),w,h,10,.625f,20,field));
    CHECK_NEAR(Sample(field,1,4),40,1e-5);
    CHECK_NEAR(Sample(field,0,4),0,1e-5);
    CHECK_NEAR(Sample(field,.5f,4),20,1e-5);
    // Terraces lose their discontinuity; constant raw inputs retain the exact scaling.
    std::fill(wet.begin(),wet.end(),255);
    std::fill(heights.begin(),heights.end(),4);
    CHECK(Build(heights.data(),wet.data(),shore.data(),w,h,10,.625f,4,field));
    for(float d:field.depth) CHECK_NEAR(d,6,Cap/255);
    for(int y=0;y<h;++y)for(int x=w/2;x<w;++x)heights[y*w+x]=0;
    CHECK(Build(heights.data(),wet.data(),shore.data(),w,h,10,.625f,4,field));
    CHECK(field.depth[4*w+7]>6 && field.depth[4*w+7]<field.depth[4*w+8]);
    CHECK(field.depth[4*w+8]<16);
    CHECK(Build(heights.data(),wet.data(),shore.data(),w,h,10,.625f,4,again));
    CHECK(field.depth==again.depth);
    for(float d:field.depth)CHECK(d>=0 && d<=Cap && std::abs(d-Encode(d)*(Cap/255))<1e-6);
    CHECK(Gain(2,24)<Gain(2,12) && Gain(2,12)<Gain(2,4));
    CHECK_NEAR(Gain(4),1,1e-6);
    CHECK(!Build(nullptr,wet.data(),shore.data(),w,h,10,.625f,4,again));
    CHECK(again.depth.empty());
    CHECK(!Build(heights.data(),wet.data(),shore.data(),w,h,0,.625f,4,again));
    CHECK(!Build(heights.data(),wet.data(),shore.data(),w,h,10,.625f,std::numeric_limits<float>::infinity(),again));
    CHECK_NEAR(Sample(field,std::numeric_limits<float>::quiet_NaN(),0),0,1e-6);
    // Long-reach shore construction and CPU reference of each shader tile vertex.
    {
        using namespace WaterCleanShore;
        const int width=901,height=241;const float spacing=10;
        Field beach;beach.width=width;beach.height=height;beach.depth.assign(width*height,40);
        std::vector<uint8_t> water(width*height,255);
        for(int y=0;y<height;++y)for(int x=0;x<120;++x)water[y*width+x]=0;
        CHECK(WaterCleanShore::Build(beach,water.data(),spacing));
        const auto sample=[&](float x,float y){return SampleField(beach,x,y,0,1);};
        const size_t near=120*width+130,far=120*width+850;
        CHECK_NEAR(beach.shoreDistance[near],110,1e-4);
        CHECK_NEAR(beach.shoreDistance[far],3000,1e-4);
        CHECK_EQ(beach.shoreConfidence[far],0);
        CHECK_EQ(beach.shoreConfidence[120*width+119],0);
        CHECK_EQ(beach.shoreDistance[120*width+119],0);
        CHECK_EQ((Word(beach,near)>>8)&255,Encode(beach.depth[near]));
        CHECK((Word(beach,near)>>24)>=128);
        CHECK_EQ((DistanceWord(beach,near)>>16)&255,EncodeDistance(110));
        CHECK_NEAR(sample(130,120).distance,EncodeDistance(110)*(3000.f/255),1e-4);
        CHECK_NEAR(sample(130,120).x,-1,1e-6);CHECK_NEAR(sample(130,120).y,0,1e-6);
        CHECK(sample(180,120).confidence>.9); // still strong >600 units from land, even with saturated D
        Settings settings;settings.turn=1;settings.reach=800;settings.maxTurn=180; // explicit legacy full-alignment fixture
        const float pi=3.141592654f;
        // Heading is uncapped, including exact antiparallel, partial follow and
        // invalid/degenerate field fallback. Only the extra shore pull is capped.
        Settings paths;paths.maxTurn=0;
        CHECK_NEAR(AppliedAngle(1,0,-1,0,{0,1,1,0},paths),pi,1e-6);
        paths.follow=.5f;
        CHECK_NEAR(AppliedAngle(1,0,-1,0,{0,1,1,0},paths),pi/2,1e-6);
        CHECK_EQ(HeadingAngle(1,0,0,0,1),0);
        CHECK_EQ(HeadingAngle(1,0,1e-8f,-1e-8f,1),0);
        CHECK_EQ(HeadingAngle(1,0,0,1e-5f,1),0);
        paths.follow=1;paths.turn=1;paths.maxTurn=30;
        CHECK_NEAR(AppliedAngle(1,0,0,1,{-1,0,1,0},paths),pi/2+pi/6,1e-6);
        CHECK_NEAR(AppliedAngle(1,0,0,1,{1,0,1,0},paths),pi/2-pi/6,1e-6);
        paths.follow=0;paths.turn=0;
        for(int degrees=-180;degrees<=180;++degrees) {
            const float r=degrees*pi/180;
            CHECK_EQ(AppliedAngle(1,0,std::cos(r),std::sin(r),{-1,0,1,0},paths),0);
        }
        auto angle=VertexAngle(0,1,sample(130,120),settings);
        CHECK(angle>0 && angle<=pi/2); // parallel travel rotates CCW toward the west beach
        CHECK(-std::sin(angle)*sample(130,120).x>0);
        angle=VertexAngle(1,0,sample(130,120),settings);
        CHECK(angle>0 && angle<=pi); // offshore travel takes the shortest turn, never exceeds delta
        CHECK(std::cos(angle)*sample(130,120).x>0);
        CHECK_NEAR(VertexAngle(1,0,{-1,0,1,0},settings),pi,1e-6); // exact 180: CCW tie
        CHECK_NEAR(VertexAngle(1,-0.f,{-1,-0.f,1,0},settings),pi,1e-6);
        settings.turn=.25;
        CHECK_NEAR(VertexAngle(1,0,{-1,0,1,0},settings),pi*.25f,1e-6);
        settings.turn=1;
        CHECK_NEAR(VertexAngle(0,1,{-1,0,1,400},settings),pi/4,1e-6);
        {
            Settings limited;limited.turn=1;
            CHECK_EQ(limited.maxTurn,60);
            CHECK_NEAR(VertexAngle(1,0,{-1,0,1,0},limited),pi/3,1e-6);
            CHECK_NEAR(VertexAngle(1,0,{0,-1,1,0},limited),-pi/3,1e-6);
            limited.turn=.25;
            // Clamp the final weighted turn, rather than the initial shore delta.
            CHECK_NEAR(VertexAngle(1,0,{-1,0,1,0},limited),pi/4,1e-6);
            for(float maximum:{0.f,30.f,60.f,120.f,180.f})
                for(float strength:{0.f,.25f,.8f,1.f})
                    for(float confidence:{0.f,.05f,.1f,.2f,1.f})
                        for(float distance:{0.f,200.f,600.f,800.f,1200.f})
                            for(int degrees=0;degrees<180;degrees+=7) {
                                limited.maxTurn=maximum;limited.turn=strength;
                                const float radians=degrees*pi/180;
                                const WaterCleanShore::Sample plus={std::cos(radians),std::sin(radians),confidence,distance};
                                const WaterCleanShore::Sample minus={plus.x,-plus.y,confidence,distance};
                                const float angle=VertexAngle(1,0,plus,limited),opposite=VertexAngle(1,0,minus,limited);
                                const float t=(std::min)(1.f,distance/limited.reach);
                                const float c=(std::max)(0.f,(std::min)(1.f,(confidence-.02f)/(.15f-.02f)));
                                const float old=std::atan2(plus.y,plus.x)*strength*(c*c*(3-2*c))*(1-t*t*(3-2*t));
                                CHECK(std::abs(angle)<=maximum*(pi/180));
                                CHECK_EQ(angle,-opposite);
                                CHECK_NEAR(angle,(std::min)(old,maximum*(pi/180)),1e-6);
                                if(maximum==180)CHECK_EQ(angle,old);
                            }
        }
        for(int degree=-180;degree<=180;degree+=15) {
            const float radians=degree*pi/180,wx=std::cos(radians),wy=std::sin(radians);
            for(float confidence:{0.f,.2f,.7f,1.f}) {
                const WaterCleanShore::Sample shoreward={-1,0,confidence,250};
                const float turn=VertexAngle(wx,wy,shoreward,settings);
                const float rx=std::cos(turn)*wx-std::sin(turn)*wy;
                CHECK(-rx>=-wx-1e-6f); // shore alignment never worsens, even for initially offshore wind
                CHECK(std::abs(turn)<=std::acos((std::max)(-1.f,(std::min)(1.f,-wx)))+1e-6f);
            }
        }
        CHECK_EQ(VertexAngle(0,1,sample(210,120),settings),0); // 910 > reach 800
        // Bilinear lookup and distance packing cannot extend the reach gate.
        CHECK_EQ(VertexAngle(0,1,sample(219.5f,120.5f),settings),0);
        const auto distance=beach.shoreDistance;const auto confidence=beach.shoreConfidence,x=beach.shoreX,y=beach.shoreY;
        std::fill(beach.depth.begin(),beach.depth.end(),0); // construction is independent of D
        CHECK(WaterCleanShore::Build(beach,water.data(),spacing));
        CHECK(distance==beach.shoreDistance && confidence==beach.shoreConfidence && x==beach.shoreX && y==beach.shoreY);
        std::fill(water.begin(),water.end(),255);
        CHECK(WaterCleanShore::Build(beach,water.data(),spacing));
        for(size_t i=0;i<water.size();++i) {CHECK_EQ(beach.shoreDistance[i],3000);CHECK_EQ(beach.shoreConfidence[i],0);}
        CHECK(!WaterCleanShore::Build(beach,water.data(),std::numeric_limits<float>::quiet_NaN()));
        CHECK(beach.shoreDistance.empty());

        // Opposing shores of a 1000-unit-wide channel: smooth vectors cancel at
        // the centre, while both halves turn well beyond the old 100-unit band.
        Field channel;channel.width=401;channel.height=241;channel.depth.assign(401*241,40);
        water.assign(401*241,0);
        for(int yy=0;yy<241;++yy)for(int xx=151;xx<250;++xx)water[yy*401+xx]=255;
        CHECK(WaterCleanShore::Build(channel,water.data(),spacing));
        const auto centre=SampleField(channel,200,120,0,1);
        CHECK_NEAR(centre.distance,500,3000.f/255);
        CHECK(centre.confidence<.01);CHECK_NEAR(VertexAngle(0,1,centre,settings),0,.01);
        for(float reach:{800.f,1200.f}) {
            settings.reach=reach;
            for(int xx:{160,175,190,210,225,240}) {
                const auto bank=SampleField(channel,float(xx),120,0,1);
                angle=VertexAngle(0,1,bank,settings);
                CHECK(bank.distance<reach && bank.confidence>.01);
                CHECK(xx<200?angle>0:angle<0);
                CHECK(std::abs(angle)>.005); // whole width apart from the intentional medial-axis null
            }
        }
        // A U-shaped bay opens into broad water; its 1000-unit cross-section
        // retains shoreward turns on both sides with reach 800+.
        Field bay;bay.width=401;bay.height=601;bay.depth.assign(401*601,40);
        water.assign(401*601,0);
        for(int yy=101;yy<601;++yy)for(int xx=0;xx<401;++xx)
            if((xx>150 && xx<250) || yy>500)water[yy*401+xx]=255;
        CHECK(WaterCleanShore::Build(bay,water.data(),spacing));
        for(float reach:{800.f,1200.f}) {
            settings.reach=reach;
            for(int xx:{160,175,190,210,225,240}) {
                const auto bank=SampleField(bay,float(xx),300,0,1);
                angle=VertexAngle(0,1,bank,settings);
                CHECK(bank.distance<reach && bank.confidence>.01);
                CHECK(xx<200?angle>0:angle<0);CHECK(std::abs(angle)>.005);
            }
        }
        // Diagonal chamfer distance and single-land-cell direction are not tied to D.
        Field island;island.width=241;island.height=241;island.depth.assign(241*241,40);
        water.assign(241*241,255);water[120*241+120]=0;
        CHECK(WaterCleanShore::Build(island,water.data(),spacing));
        CHECK_NEAR(island.shoreDistance[121*241+121],spacing*std::sqrt(2.f),1e-4);
        CHECK(SampleField(island,150,120,0,1).x<-.99);
        CHECK(SampleField(island,120,150,0,1).y<-.99);
        // Narrow water beside a large western/southern land mass and a thin
        // northern bank: wide smoothing used to point sideways / across the bank.
        Field local;local.width=601;local.height=401;local.depth.assign(601*401,40);
        water.assign(601*401,255);
        for(int yy=0;yy<401;++yy)for(int xx=0;xx<601;++xx)
            if(xx<150 || yy<=180 || (yy>=200 && yy<=209 && xx<500))water[yy*601+xx]=0;
        CHECK(WaterCleanShore::Build(local,water.data(),spacing));
        const auto nearBank=SampleField(local,230,198,1,0);
        CHECK(nearBank.y>.99 && std::abs(nearBank.x)<.01 && nearBank.confidence>.5);
        CHECK(VertexAngle(1,0,nearBank,Settings{1,1500,300,3,2,60})>0);
        CHECK(SampleField(local,230,190,1,0).confidence<.01);
        for(float yy=189.75f;yy<=190.25f;yy+=.025f) {
            const auto medial=SampleField(local,230,yy,1,0);
            CHECK(medial.confidence<.15); // direction reversal is gated out on the centreline
        }
        // A straight open coast retains its exact normal across all four scales.
        Field wide;wide.width=601;wide.height=301;wide.depth.assign(601*301,40);
        water.assign(601*301,255);
        for(int yy=0;yy<301;++yy)for(int xx=0;xx<60;++xx)water[yy*601+xx]=0;
        CHECK(WaterCleanShore::Build(wide,water.data(),spacing));
        for(int xx=61;xx<260;++xx) {
            const auto coast=SampleField(wide,float(xx),150,0,1);
            CHECK_NEAR(coast.x,-1,1e-6);CHECK_NEAR(coast.y,0,1e-6);
            CHECK(coast.confidence>.5);
        }
        // Oblique distance ramps around a round island change smoothly through
        // each support transition, including confidence after RGBA8 packing.
        Field round;round.width=601;round.height=401;round.depth.assign(601*401,40);
        water.assign(601*401,255);
        for(int yy=0;yy<401;++yy)for(int xx=0;xx<601;++xx)
            if(std::hypot(float(xx-160),float(yy-200))<=60)water[yy*601+xx]=0;
        CHECK(WaterCleanShore::Build(round,water.data(),spacing));
        auto last=SampleField(round,220,230,0,1);
        for(float xx=220.25f;xx<450;xx+=.25f) {
            auto next=SampleField(round,xx,230,0,1);
            CHECK(std::hypot(next.x-last.x,next.y-last.y)<.025);
            CHECK(std::abs(next.confidence-last.confidence)<.025);last=next;
        }
        for(float distance:{80.f,240.f,500/.75f,1000/.75f}) {
            float sum=0;
            for(int scale=0;scale<4;++scale) {
                sum+=ScaleWeight(distance,scale);
                CHECK(std::abs(ScaleWeight(distance-.01f,scale)-ScaleWeight(distance+.01f,scale))<.001);
            }
            CHECK_NEAR(sum,1,1e-6);
        }
        // Enclosed harbour opening SOUTH (-Y): broad southern sea should travel
        // north into it, despite opposing local normals in the harbour channel.
        Field harbour;harbour.width=401;harbour.height=601;harbour.depth.assign(401*601,40);
        water.assign(401*601,0);
        for(int yy=0;yy<601;++yy)for(int xx=0;xx<401;++xx)
            if(yy<200 || (xx>150 && xx<250 && yy<500))water[yy*401+xx]=255;
        CHECK(WaterCleanShore::Build(harbour,water.data(),spacing));
        // Symmetric island group still builds an optional shore-pull field.
        Field archipelago;archipelago.width=401;archipelago.height=401;archipelago.depth.assign(401*401,40);
        water.assign(401*401,255);
        for(int yy=0;yy<401;++yy)for(int xx=0;xx<401;++xx)
            for(int cy:{140,260})for(int cx:{140,260})
                if((xx-cx)*(xx-cx)+(yy-cy)*(yy-cy)<=25*25)water[yy*401+xx]=0;
        CHECK(WaterCleanShore::Build(archipelago,water.data(),spacing));
        // The powered barycentric weights have unit squared norm; differentiate its normalization.
        for(double sharp:{1.,2.,8.}) {
            const double b[]={.2,.3,.5},db[]={-1.,1.,0.};double a[3],da[3],norm2=0,dot=0;
            for(int i=0;i<3;++i) {a[i]=std::pow(b[i],sharp);da[i]=sharp*std::pow(b[i],sharp-1)*db[i];norm2+=a[i]*a[i];dot+=a[i]*da[i];}
            double sum=0,orthogonal=0;
            for(int i=0;i<3;++i) {
                double weight=a[i]/std::sqrt(norm2),derivative=da[i]/std::sqrt(norm2)-a[i]*dot/std::pow(norm2,1.5);
                double norms[2]={0,0},weights[2];for(int side=0;side<2;++side) {double e=(side?1:-1)*1e-6;for(int j=0;j<3;++j)norms[side]+=std::pow(b[j]+e*db[j],2*sharp);weights[side]=std::pow(b[i]+e*db[i],sharp)/std::sqrt(norms[side]);}
                CHECK_NEAR(derivative,(weights[1]-weights[0])/2e-6,1e-7);sum+=weight*weight;orthogonal+=weight*derivative;
            }
            CHECK_NEAR(sum,1,1e-12);CHECK_NEAR(orthogonal,0,1e-12);
        }
    }
    // Full transformed vector-field chain: R*J*R^-1, normalized-weight gradients,
    // then the D amplitude product rule. Numerical differences use displacement only.
    {
        const auto evaluate=[](double px,double py,bool analytic,int axis) {
            const double b[]={1-px-py,px,py},dbx[]={-1,1,0},dby[]={-1,0,1};
            const double angles[]={-.7,.2,1.1};double a[3],ax[3],ay[3],norm=0,dotx=0,doty=0;
            for(int i=0;i<3;++i) {a[i]=b[i]*b[i]*b[i];ax[i]=3*b[i]*b[i]*dbx[i];ay[i]=3*b[i]*b[i]*dby[i];norm+=a[i]*a[i];dotx+=a[i]*ax[i];doty+=a[i]*ay[i];}
            std::vector<double> d(3),dx(3),dy(3);
            for(int i=0;i<3;++i) {
                const double c=std::cos(angles[i]),q=std::sin(angles[i]),x=c*(px-i*.1)+q*(py+i*.05),y=-q*(px-i*.1)+c*(py+i*.05);
                const double raw[]={std::sin(x)+y*y,x*y,std::cos(y)+x*x};
                const double rx[]={std::cos(x),y,2*x},ry[]={2*y,x,-std::sin(y)};
                double v[]={c*raw[0]-q*raw[1],q*raw[0]+c*raw[1],raw[2]};
                double ux[3],uy[3];for(int k=0;k<3;++k) {ux[k]=rx[k]*c-ry[k]*q;uy[k]=rx[k]*q+ry[k]*c;}
                const double x0=ux[0],y0=uy[0];ux[0]=c*x0-q*ux[1];ux[1]=q*x0+c*ux[1];uy[0]=c*y0-q*uy[1];uy[1]=q*y0+c*uy[1];
                const double w=a[i]/std::sqrt(norm),wx=ax[i]/std::sqrt(norm)-a[i]*dotx/std::pow(norm,1.5),wy=ay[i]/std::sqrt(norm)-a[i]*doty/std::pow(norm,1.5);
                for(int k=0;k<3;++k) {d[k]+=w*v[k];dx[k]+=w*ux[k]+wx*v[k];dy[k]+=w*uy[k]+wy*v[k];}
            }
            const double gain=.5+.2*px+.3*py;
            for(int k=0;k<3;++k) {dx[k]=dx[k]*gain+.2*d[k];dy[k]=dy[k]*gain+.3*d[k];d[k]*=gain;}
            return analytic?(axis?dy:dx):d;
        };
        for(int axis=0;axis<2;++axis) {
            const double e=1e-6;auto a=evaluate(.2,.3,true,axis),lo=evaluate(.2-(axis?0:e),.3-(axis?e:0),false,axis),hi=evaluate(.2+(axis?0:e),.3+(axis?e:0),false,axis);
            for(int k=0;k<3;++k)CHECK_NEAR(a[k],(hi[k]-lo[k])/(2*e),1e-7);
        }
    }
    CHECK_NEAR(Gain(4,0),0,1e-6);
    CHECK_EQ(Encode(std::numeric_limits<float>::infinity()),0);
}
}

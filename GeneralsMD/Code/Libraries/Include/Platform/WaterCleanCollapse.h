/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <cmath>
#include <algorithm>

namespace WaterCleanCollapse {
// Shader-generation constants, deliberately independent of the live look controls.
constexpr float AngleDegrees=1.5f;
constexpr float AngleRadians=AngleDegrees*3.14159265358979323846f/180.f;
constexpr float ShadingLod=0.25f;
// Rotations are periodic. Unwrap around vertex zero before taking the mean.
inline float Delta(float a) {constexpr float pi=3.14159265358979323846f;return a-2*pi*std::floor((a+pi)/(2*pi));}
struct Mean {bool collapse;float angle,dx,dy,scale,scaleX,scaleY;};
// Each tile vertex v turns its copy about itself: q=v+R(-a)*(p-v). Equal turns are not
// enough for one read; |q-p|=2*sin(|a|/2)*|p-v| reaches the tile size at large turns, and
// a single read about the moving mean pivot then stretches and folds the waves. One read
// is the three-copy blend only where every turn is small, so the test is on each angle.
inline bool Collapses(const float (&angle)[3]) {
    for(float a:angle)if(!(std::fabs(Delta(a))<AngleRadians))return false;
    return true;
}
inline Mean Evaluate(const float (&angle)[3],const float (&weight)[3],
    const float (&dx)[3],const float (&dy)[3]) {
    float a[3]={angle[0],angle[0]+Delta(angle[1]-angle[0]),angle[0]+Delta(angle[2]-angle[0])};
    Mean m={};
    m.collapse=Collapses(angle);
    float offset=0,offsetX=0,offsetY=0;
    for(int i=0;i<3;++i) {
        m.scale+=weight[i];m.scaleX+=dx[i];m.scaleY+=dy[i];
        offset+=weight[i]*(a[i]-a[0]);offsetX+=dx[i]*(a[i]-a[0]);offsetY+=dy[i]*(a[i]-a[0]);
    }
    if(m.scale>0) {
        const float meanOffset=offset/m.scale;
        m.angle=a[0]+meanOffset;
        m.dx=(offsetX-meanOffset*m.scaleX)/m.scale;
        m.dy=(offsetY-meanOffset*m.scaleY)/m.scale;
    }
    return m;
}
}

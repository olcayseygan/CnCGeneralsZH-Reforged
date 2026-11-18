/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <cmath>
namespace WaterCleanSpectrum {
constexpr double Pi=3.14159265358979323846,Gravity=9.81;
struct Parameters {double windSpeed=8.7,fetch=20000,swell=.7,sea2Share=.33,sea2Angle=135,sea2Wavelength=600,sea2Spread=12,spread=0;};
inline const Parameters &Settings(){static const Parameters p;return p;}
inline double PeakFrequency(const Parameters &p) {return 22.0*std::cbrt(Gravity*Gravity/(p.windSpeed*p.fetch));}
inline double Jonswap(double omega,const Parameters &p) {
    if(!(omega>0))return 0;
    const double peak=PeakFrequency(p),sigma=omega<=peak?0.07:0.09;
    const double r=std::exp(-0.5*std::pow((omega-peak)/(sigma*peak),2));
    const double alpha=0.076*std::pow(p.windSpeed*p.windSpeed/(Gravity*p.fetch),0.22);
    return alpha*Gravity*Gravity*std::pow(omega,-5)*std::exp(-1.25*std::pow(peak/omega,4))*std::pow(3.3,r);
}
// Hasselmann/Longuet-Higgins, Horvath 2015: s_xi=16*tanh(wp/w)*xi^2.
// Exact log-gamma normalization over [-pi,pi], rather than an approximate fit.
inline double SpreadingExponent(double omega,const Parameters &p) {
    const double peak=PeakFrequency(p),ratio=omega/peak;
    const double s=omega<=peak ? 6.97*std::pow(ratio,4.06)
        : 9.77*std::pow(ratio,-2.33-1.45*(p.windSpeed*peak/Gravity-1.17));
    return s+16.0*std::tanh(peak/omega)*p.swell*p.swell;
}
inline double Spreading(double theta,double omega,const Parameters &p) {
    if(!(omega>0))return 0;
    const double s=SpreadingExponent(omega,p);
    const double cosine=std::abs(std::cos(theta*0.5));
    if(cosine<1e-15)return p.spread==0?0:p.spread/(2*Pi);
    const double directional=std::exp(std::lgamma(s+1)-std::lgamma(s+0.5)-std::log(2*std::sqrt(Pi))+2*s*std::log(cosine));
    // Preserve the original arithmetic exactly when broadening is disabled.
    return p.spread==0?directional:(1-p.spread)*directional+p.spread/(2*Pi);
}
// Secondary swell uses a log-normal wavelength envelope and normalized cos^(2s)
// half-angle directional density. Wavelength is expressed in game-world units.
inline double SecondaryDensity(double wavelength,double theta,const Parameters &p) {
    const double cosine=std::abs(std::cos(theta*.5));
    if(cosine<1e-15 || !(wavelength>0))return 0;
    const double radial=std::log(wavelength/p.sea2Wavelength)/.35;
    return std::exp(-.5*radial*radial+std::lgamma(p.sea2Spread+1)
        -std::lgamma(p.sea2Spread+.5)-std::log(2*std::sqrt(Pi))
        +2*p.sea2Spread*std::log(cosine));
}
}

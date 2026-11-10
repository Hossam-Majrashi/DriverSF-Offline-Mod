#pragma once
#include <cmath>
#include <cstdint>
#include "speed_policy.h"

// User-authorized engine/TRANSMISSION tuning, NOT the old wheel-radius hack.
// Per-car target gearing, smoothly engaged in a moving car; no direct forces,
// speed/RPM/gear-state writes or target-seeking acceleration controller.
namespace PowertrainPolicy {
constexpr float rpmToRadians=0.104719758034f;
constexpr float redlineFractionAt300=0.75f;
constexpr float maxStretch=8.0f,maxWheelGain=16.0f;
constexpr float stretchRisePerSecond=0.08f; // <=0.8% coefficient change per 100-ms step
constexpr float gainRisePerSecond=0.25f; // extra torque-gain factor/s; NOT an acceleration bound
// Body angular motion includes pitch/roll as well as turning. The old
// 0.12..0.40 window starved power on ordinary course corrections/road motion.
// These are coefficient-policy thresholds, NOT validated handling limits.
constexpr float motionFilterSeconds=0.15f;
constexpr float gearingHoldMotion=0.65f,powerFadeStart=0.35f,powerFadeEnd=1.20f;
constexpr float immediateMotionCut=2.0f;
struct Stock {float torque,overall,radius,idle,redline,ratios[7];int gears;};
struct Setup {float stretch,overall,stockTopMs,maxGain;};
inline bool range(float v,float lo,float hi){return std::isfinite(v)&&v>=lo&&v<=hi;}
inline float smooth(float x){x=std::fmax(0.0f,std::fmin(1.0f,x));return x*x*x*(x*(x*6-15)+10);}
inline bool configure(const Stock&s,Setup&p,float ceiling=SpeedPolicy::ceilingMs) {
    if(!range(ceiling,1,200))return false;
    if(s.gears<1 || s.gears>6 || !range(s.torque,1,1000000) ||
       !range(s.overall,0.001f,10) || !range(s.radius,0.05f,3) ||
       !range(s.idle,1,5000) || !range(s.redline,s.idle+100,25000) ||
       !range(s.ratios[0],-100,0))return false;
    for(int i=1;i<=s.gears;i++)if(!range(s.ratios[i],0.001f,100) ||
        (i>1 && s.ratios[i]<=s.ratios[i-1]))return false;
    float top=s.redline*s.ratios[s.gears]*s.radius*s.overall*rpmToRadians;
    if(!range(top,1,1000))return false;
    float stretch=std::fmax(1.0f,ceiling/(redlineFractionAt300*top));
    if(!range(stretch,1,maxStretch) || !range(s.overall*stretch,0.001f,32))return false;
    p={stretch,s.overall*stretch,top,std::fmin(maxWheelGain,std::fmax(2.0f,stretch*stretch))};
    return true;
}
// Only meaningful native configuration fields count. Unused ratio slots can
// change independently and must never switch the feature off.
inline uint32_t changes(const Stock&a,const Stock&b) {
    uint32_t mask=(a.torque!=b.torque?1u:0u)|(a.overall!=b.overall?2u:0u)|
        (a.radius!=b.radius?4u:0u)|(a.idle!=b.idle?8u:0u)|
        (a.redline!=b.redline?16u:0u)|(a.gears!=b.gears?32u:0u);
    if(a.gears==b.gears && b.gears>=1 && b.gears<=6)
        for(int i=0;i<=b.gears;i++)if(a.ratios[i]!=b.ratios[i])mask|=64u;
    return mask;
}
inline float filteredMotion(float previous,float current,float dt) {
    if(!range(current,0,1000))return 1000;
    if(!range(previous,0,1000) || !range(dt,0,0.10001f) || dt==0)return current;
    return previous+(current-previous)*(dt/(motionFilterSeconds+dt));
}
inline bool rebaseStretch(const Stock&before,float stretch,const Stock&after,float&rebased) {
    // Retain the absolute already-engaged overall coefficient when possible.
    // Never snap back to x1 merely because native torque/redline changed.
    rebased=before.overall==after.overall?stretch:
        std::fmax(1.0f,before.overall*stretch/after.overall);
    return range(rebased,1,maxStretch) && range(after.overall*rebased,0.001f,32);
}
inline float rampStretch(float previous,float target,float dt,bool stopped,bool hold) {
    if(!range(previous,1,maxStretch) || !range(target,1,maxStretch))return 1;
    if(stopped)return target;
    if(hold || target<previous || !range(dt,0,0.10001f))return previous;
    // Same scale for ALL native forward gears. During shifts/corners/braking
    // hold it fixed; otherwise extend gently without setting RPM or a gear.
    return std::fmin(target,previous*(1+stretchRisePerSecond*dt));
}
inline float requestedGain(const Setup&p,float speed,float throttle,float brake,bool handbrake,
                           float angularSpeed,bool shifting,int gear) {
    // Intentional, disclosed progressive ENGINE power curve. No velocity/error
    // controller: these values only choose a torque coefficient for native physics.
    if(!range(speed,0,1000) || !range(throttle,0,1) || !range(brake,0,1) ||
       !range(angularSpeed,0,1000) || handbrake || brake>0.01f ||
       shifting || gear<1 || throttle<0.8f)return 1;
    float road=smooth((speed/p.stockTopMs-0.3f)/1.2f);
    float pedal=smooth((throttle-0.8f)/0.2f);
    float straight=1-smooth((angularSpeed-powerFadeStart)/(powerFadeEnd-powerFadeStart));
    return 1+(p.maxGain-1)*road*pedal*straight;
}
inline float rateLimit(float previous,float requested,float dt) {
    if(!range(previous,1,maxWheelGain) || !range(requested,1,maxWheelGain))return 1;
    if(requested<=previous)return requested; // reduce promptly when cornering/braking
    if(!range(dt,0,0.10001f))return 1; // pause/clock/car discontinuity: no catch-up boost
    return std::fmin(requested,previous+gainRisePerSecond*dt);
}
inline bool coefficients(const Stock&s,const Setup&p,float gain,float throttle,float brake,
                         bool handbrake,float&torque) {
    if(!range(gain,1,maxWheelGain) || !range(throttle,0,1) || !range(brake,0,1))return false;
    // When coasting/braking do not multiply the native engine's drag/clutch
    // torque limit by the power upgrade. Gearing is held fixed during shifts.
    if(throttle<=0.01f || brake>0.01f || handbrake)torque=s.torque;
    else torque=s.torque*p.stretch*gain;
    return range(torque,1,8000000);
}
} // namespace PowertrainPolicy

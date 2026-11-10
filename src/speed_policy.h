#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

// Ceiling only. No torque, gearing, force, velocity or acceleration policy.
namespace SpeedPolicy {
constexpr float targetMph=300.0f;
constexpr float metresPerMile=1609.344f;
constexpr float targetDisplayMs=targetMph*metresPerMile/3600.0f;
// The shipped HUD already scales high speeds by 1.3 (0x8994f0). Leave that
// conversion untouched; translate the requested HUD ceiling into simulation
// units. This is NOT a promise that a stock engine can reach this ceiling.
constexpr float nativeHighSpeedDisplayScale=1.3f;
constexpr float ceilingMs=targetDisplayMs/nativeHighSpeedDisplayScale;
constexpr size_t viewSize=0x40;
// Public handling research identifies p160. The shipped limiter reads it at
// resource+0x310+0x2be0 and converts it using this native float constant.
constexpr size_t modelLimitOffset=0x2be0;
constexpr size_t drivingViewSize=modelLimitOffset+sizeof(float);
constexpr float nativeMphToMs=0.4470192492008209f;
constexpr float nativeLimitMph=ceilingMs/nativeMphToMs;
// Optional engine upgrade: keep the native p160 taper above 300, while the
// independent input/ability ceilings remain 300. Default cap-only mode unchanged.
constexpr float performanceLimitMph=nativeLimitMph/0.89f;
constexpr int choices[]={100,200,300,400,500};
inline bool validTarget(int mph){for(int n:choices)if(mph==n)return true;return false;}
inline float displayedMph(float ms) {
    if(!std::isfinite(ms) || ms<0)return 0;
    float scale=ms<=22?1:std::fmin(1.3f,1+std::pow((ms-22)*0.00995f,1.7f));
    return ms*scale/0.44704f;
}
struct Target {int mph;float ms,model,performance;};
inline Target calculateTarget(int mph) {
    if(!validTarget(mph))mph=300;
    // Invert the ORIGINAL HUD function, including its unsaturated 100-MPH
    // region. A fixed /1.3 conversion is wrong at this lower choice.
    float lo=0,hi=300;
    for(int n=0;n<32;n++){float m=(lo+hi)*0.5f;if(displayedMph(m)<mph)lo=m;else hi=m;}
    float ms=mph==300?ceilingMs:(lo+hi)*0.5f;
    return {mph,ms,ms/nativeMphToMs,ms/nativeMphToMs/0.89f};
}
inline Target target(int mph) {
    static const Target values[]={calculateTarget(100),calculateTarget(200),calculateTarget(300),calculateTarget(400),calculateTarget(500)};
    for(const auto&t:values)if(t.mph==mph)return t;
    return values[2];
}
inline bool boostCeiling(float native,float &cap,float requested=ceilingMs,bool exact=false) {
    // Only raise a finite, positive native braking threshold. Not a force or
    // target-speed controller; the original overspeed branch remains negative.
    if(!std::isfinite(requested) || requested<=0 || requested>1000 ||
       !std::isfinite(native) || native<=0.0f || (exact?native==requested:native>=requested))return false;
    cap=requested;return true;
}
inline bool modelCeilingView(const void *nativeView,void *copy,float requested=nativeLimitMph,bool exact=false) {
    const auto *src=static_cast<const unsigned char*>(nativeView);
    float cap;std::memcpy(&cap,src+modelLimitOffset,sizeof(cap));
    // Nonpositive values disable this native limiter: do not re-enable it.
    if(!std::isfinite(requested) || requested<=0 || requested>1000 ||
       !std::isfinite(cap) || cap<=0.0f || (exact?cap==requested:cap>=requested))return false;
    std::memcpy(copy,nativeView,drivingViewSize);
    std::memcpy(static_cast<unsigned char*>(copy)+modelLimitOffset,&requested,sizeof(requested));
    return true;
}
inline bool ceilingView(const void *nativeView,void *copy,float requested=ceilingMs,bool exact=false) {
    // Native getter returns vehicle+0xb0; flags +0x3c, speed limit +0x1c.
    // Alter only a caller-owned snapshot; never a vehicle or shared resource.
    const auto *src=static_cast<const unsigned char*>(nativeView);
    if(!std::isfinite(requested) || requested<=0 || requested>1000)return false;
    uint16_t flags;float cap;
    std::memcpy(&flags,src+0x3c,sizeof(flags));
    std::memcpy(&cap,src+0x1c,sizeof(cap));
    if(flags&0x10) {
        if(!std::isfinite(cap) || cap<0.0f || cap>1000.0f)return false;
        if(!exact && cap>requested)return false; // legacy raise-only mode
    }
    std::memcpy(copy,nativeView,viewSize);
    flags|=0x10;
    std::memcpy(static_cast<unsigned char*>(copy)+0x1c,&requested,sizeof(requested));
    std::memcpy(static_cast<unsigned char*>(copy)+0x3c,&flags,sizeof(flags));
    return true;
}
} // namespace SpeedPolicy

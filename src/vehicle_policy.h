#pragma once
#include <cmath>
#include <cstdint>

// Pure policies shared with offline tests; no game pointers or Windows calls.
namespace VehiclePolicy {
inline bool positive(float v) { return std::isfinite(v) && v>0.0f && v<=360000.0f; }
inline float activeHeat(float heat) {
    return std::isfinite(heat) && heat>0.5f && heat<=1.0f ? 0.5f : heat;
}
inline float remaining(int beforeState,int afterState,float before,float after,float sustain) {
    // Never restart an idle/released/overheated ability or revive an exhausted one.
    // The initial timer includes warm-up + sustain + wind-down. Freezing it
    // before the sustain window would lock the engine in weak warm-up forever.
    return beforeState==1 && afterState==1 && positive(before) && positive(after)
        && positive(sustain) && before<=sustain && after<=sustain
        && after<before ? before : after;
}
inline bool charge(int state,float duration,float elapsed) {
    return state==1 && positive(duration) && std::isfinite(elapsed) && elapsed>=0.0f;
}
inline bool amplify(const float *before,const float *after,float *out) {
    // Double only RAM's native XYZ contribution, never the existing input.
    for(int i=0;i<3;i++) {
        out[i]=after[i]+(after[i]-before[i]);
        if(!std::isfinite(before[i]) || !std::isfinite(after[i]) ||
           !std::isfinite(out[i]) || std::fabs(out[i])>100000.0f)return false;
    }
    return true;
}
inline uintptr_t collisionTarget(uintptr_t player,uintptr_t a,uintptr_t b) {
    if(!player || !a || !b || a==b)return 0;
    if(a==player && b!=player)return b;
    if(b==player && a!=player)return a;
    return 0;
}
} // namespace VehiclePolicy

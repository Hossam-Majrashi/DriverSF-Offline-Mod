#pragma once
#include <stdint.h>
namespace PaintPolicy {
struct Selection { uint8_t indices[4]; uint32_t count; };
static_assert(sizeof(Selection)==8,"Native paint selection is two 32-bit words");
inline bool valid(const Selection &s,uint32_t groups,const uint32_t counts[3]) {
    if(groups<1 || groups>3 || s.count!=groups)return false;
    for(uint32_t i=0;i<groups;i++)
        if(counts[i]<1 || counts[i]>16 || s.indices[i]>=counts[i])return false;
    return true;
}
inline bool choose(const Selection &current,uint32_t groups,const uint32_t counts[3],
                   int variant,Selection &input,Selection &normalized) {
    if(!valid(current,groups,counts) || variant<0 || uint32_t(variant)>=counts[0])return false;
    input=current;input.indices[0]=uint8_t(variant);normalized=input;
    // Native 8cf920 copies the primary index to group 3, then normalizes each group.
    if(groups>2)normalized.indices[2]=uint8_t(uint32_t(variant)%counts[2]);
    return true;
}
inline bool same(const Selection &a,const Selection &b) {
    if(a.count!=b.count || a.count>3)return false;
    for(uint32_t i=0;i<a.count;i++)if(a.indices[i]!=b.indices[i])return false;
    return true;
}
inline bool fresh(uint32_t now,uint32_t deadline) {return int32_t(deadline-now)>=0;}
}

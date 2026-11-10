#pragma once
namespace SignalInputPolicy {
static constexpr int keys[]={0,0x5a,0x58,0x43,0x4a,0x4b,0x4c,0x26,0x28,0x25,0x27,0x54,0x47,0x42,0x4e,0x4d,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x7b};
static constexpr const char*keyNames[]={"None","Z","X","C","J","K","L","Up arrow","Down arrow","Left arrow","Right arrow","T","G","B","N","M","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};
static constexpr uint32_t pads[]={0,1,2,4,8,0x1000,0x2000,0x4000,0x8000,0x100,0x200,0x20,0x10,0x40,0x80,1u<<16,1u<<17};
static constexpr const char*padNames[]={"None","D-pad Up","D-pad Down","D-pad Left","D-pad Right","A / Cross","B / Circle","X / Square","Y / Triangle","LB / L1","RB / R1","Back / Share","Start / Options","Left stick click","Right stick click","LT / L2","RT / R2"};
static constexpr unsigned keyCount=sizeof(keys)/sizeof(*keys),padCount=sizeof(pads)/sizeof(*pads);
static int keyIndex(int key){for(unsigned i=0;i<keyCount;i++)if(keys[i]==key)return int(i);return -1;}
static bool valid(const int values[3],bool keyboard){
    for(unsigned i=0;i<3;i++){
        if(keyboard?keyIndex(values[i])<0:values[i]<0 || unsigned(values[i])>=padCount)return false;
        for(unsigned j=0;j<i;j++)if(values[i] && values[i]==values[j])return false;
    }return true;
}
struct Edges {
    bool previousKeys[3]{},wasActive=false,wasConnected=false;
    uint32_t previousPad=0;
    unsigned poll(bool active,bool connected,const bool down[3],uint32_t pad,const int bindings[3]){
        unsigned actions=0;
        if(active && wasActive)for(unsigned i=0;i<3;i++){
            bool key=down[i]&&!previousKeys[i];
            bool button=connected && wasConnected && bindings[i]>=0 && unsigned(bindings[i])<padCount
                && (pads[bindings[i]] & pad & ~previousPad)!=0;
            if(key || button)actions|=1u<<i;
        }
        for(unsigned i=0;i<3;i++)previousKeys[i]=down[i];
        previousPad=connected?pad:0;wasActive=active;wasConnected=connected;
        // Hazards has explicit priority. Ambiguous simultaneous left+right is
        // ignored, not converted into repeated contradictory native requests.
        return actions&4?3:actions==1?1:actions==2?2:0;
    }
};
}

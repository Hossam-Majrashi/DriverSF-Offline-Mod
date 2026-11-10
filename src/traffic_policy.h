#pragma once
// A 1..100 traffic LEVEL, not a literal vehicle count/percentage. Native-sorted
// candidates are compacted stably; required script/permanent lanes always stay.
namespace TrafficPolicy {
struct Lane {uintptr_t road;uint32_t direction;};
static_assert(sizeof(Lane)==8,"Native lane records require the x86 ABI");
static constexpr unsigned capacity=512;
static bool same(const Lane&a,const Lane&b){return a.road==b.road && a.direction==b.direction;}
static uint32_t priority(const Lane&x) {
    uint32_t h=uint32_t(x.road>>2)^(x.direction*0x9e3779b9u);
    h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;h^=h>>16;
    return h;
}
static unsigned quota(unsigned eligible,int level) {
    if(!eligible || level<1 || level>100)return 0;
    // Exact endpoints: 1 = one eligible group, 100 = ALL native groups.
    // Intermediate levels grow monotonically, without empty hash buckets.
    return 1+(eligible-1)*unsigned(level-1)/99;
}
// Validate the WHOLE batch before the first write. No car/pool/road-data writes.
static bool filter(Lane*lanes,unsigned&n,const Lane*protectedLanes,unsigned p,int level,
                   const Lane*retained=nullptr,unsigned r=0,const Lane*seen=nullptr,unsigned s=0) {
    if(n>capacity || p>capacity || r>capacity || s>capacity || level<1 || level>100
        || (n && !lanes) || (p && !protectedLanes) || (r && !retained) || (s && !seen))return false;
    for(unsigned i=0;i<n;i++)if(!lanes[i].road || lanes[i].direction>1)return false;
    for(unsigned i=0;i<p;i++)if(!protectedLanes[i].road || protectedLanes[i].direction>1)return false;
    for(unsigned i=0;i<r;i++)if(!retained[i].road || retained[i].direction>1)return false;
    for(unsigned i=0;i<s;i++)if(!seen[i].road || seen[i].direction>1)return false;
    bool selected[capacity]={};
    unsigned order[capacity],eligible=0,newCount=0,incumbents=0;
    uint32_t priorities[capacity];
    for(unsigned i=0;i<n;i++) {
        for(unsigned j=0;j<p;j++)if(same(lanes[i],protectedLanes[j])){selected[i]=true;break;}
        if(selected[i])continue;
        ++eligible;
        for(unsigned j=0;j<r;j++)if(same(lanes[i],retained[j])){selected[i]=true;break;}
        if(selected[i]){++incumbents;continue;}
        bool rejected=false;
        for(unsigned j=0;j<s;j++)if(same(lanes[i],seen[j])){rejected=true;break;}
        if(rejected)continue; // no quota-driven late admission on camera/pool churn
        uint32_t key=priority(lanes[i]);unsigned j=newCount++;
        // Rank the CURRENT eligible set, not an absolute hash threshold. Ties
        // retain native order. The same list/level produces the same subset.
        while(j && priorities[j-1]>key){priorities[j]=priorities[j-1];order[j]=order[j-1];--j;}
        priorities[j]=key;order[j]=i;
    }
    // Admission-only quota: NEVER evict an incumbent that the native builder
    // still wants. A lower level converges only as old groups leave naturally.
    unsigned target=quota(eligible,level),keep=target>incumbents?target-incumbents:0;
    if(keep>newCount)keep=newCount;
    for(unsigned i=0;i<keep;i++)selected[order[i]]=true;
    unsigned out=0;
    for(unsigned i=0;i<n;i++)if(selected[i])lanes[out++]=lanes[i];
    n=out;return true;
}
}

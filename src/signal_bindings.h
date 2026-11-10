#pragma once
#include "signal_input_policy.h"
namespace SignalControls {
static std::atomic<int>keyboard[3]={{0x5a},{0x43},{0x58}};
static std::atomic<int>controller[3]={{3},{4},{1}};
static char bindingStatus[160]="Choose keys/buttons, then save safe preferences.";
static void loadBindings(const int inputKeys[3],const int inputPad[3]){
    const int fallbackKeys[]={0x5a,0x43,0x58},fallbackPad[]={3,4,1};
    const int*k=SignalInputPolicy::valid(inputKeys,true)?inputKeys:fallbackKeys;
    const int*p=SignalInputPolicy::valid(inputPad,false)?inputPad:fallbackPad;
    for(unsigned i=0;i<3;i++){keyboard[i]=k[i];controller[i]=p[i];}
}
static bool choose(unsigned action,int value,bool key){
    if(action>=3)return false;int v[3];auto*target=key?keyboard:controller;
    for(unsigned i=0;i<3;i++)v[i]=target[i];v[action]=value;
    if(!SignalInputPolicy::valid(v,key)){
        strcpy(bindingStatus,"Binding unavailable or already assigned to another signal.");return false;
    }
    target[action]=value;strcpy(bindingStatus,"Binding changed. Save safe preferences to keep it.");return true;
}
}

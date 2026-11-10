#pragma once
#include <string>
#include <cstring>
#include <cstdio>
namespace UiText {
// Safe preference only: 0=Arabic (first run), 1=English. No gameplay switch.
static int language=0;
struct Translation {const char*english;const char*visual;const char*wrapped;};
#include "ui_strings.h"
static const char*lookup(const char*s,bool wrap=false) {
    if(language==1 || !s)return s?s:"";
    for(const auto&e:translations)if(!std::strcmp(e.english,s))return wrap?e.wrapped:e.visual;
    return s; // Technical diagnostics intentionally retain their original English.
}
static std::string display(const char*s,bool wrap=false) {
    const char*t=lookup(s,wrap);
    if(t!=s || language==1)return t;
    // These strings were formatted on the game thread; preserve raw logs and
    // translate only at display time, including after changing UI language.
    int n=0,before=0,after=0;char result[24];char out[640];
    if(std::sscanf(s,"Enter whole digits only: 0..%d.",&n)==1) {
        std::snprintf(out,sizeof(out),lookup("Enter whole digits only: 0..%d.",wrap),n);return out;
    }
    if(std::sscanf(s,"Color %d selected.",&n)==1) {
        std::snprintf(out,sizeof(out),lookup("Color %d selected.",wrap),n);return out;
    }
    if(std::sscanf(s,"Apply %23[^:]: %d -> %d WP (read back from engine).",result,&before,&after)==3) {
        std::snprintf(out,sizeof(out),lookup("Balance before: %d WP",wrap),before);
        std::string text=display(!std::strcmp(result,"OK")?"Success":"Failed",wrap)+"\n"+out;
        std::snprintf(out,sizeof(out),lookup("Balance after: %d WP",wrap),after);return text+"\n"+out;
    }
    return s;
}
static std::string label(const char*s) {
    // Same ID in either language: switching cannot reassign sliders/tab actions.
    return display(s)+"###"+s;
}
} // namespace UiText

#pragma once
#include "ui_text.h"
#include "ui_font.h"
#include <cstdarg>
namespace Ui {
static void initFont() {
    ImFontConfig config;config.FontDataOwnedByAtlas=false;
    static const ImWchar ranges[]={0x20,0xff,0x600,0x6ff,0x2000,0x206f,0xfb50,0xfdff,0xfe70,0xfeff,0};
    ImFont*f=ImGui::GetIO().Fonts->AddFontFromMemoryTTF(uiFontData,sizeof(uiFontData),16,&config,ranges);
    if(f)ImGui::GetIO().FontDefault=f;
}
static std::string format(const char*fmt,va_list ap,bool wrap) {
    if(!strcmp(fmt,"%s"))return UiText::display(va_arg(ap,const char*),wrap);
    char b[2048];vsnprintf(b,sizeof(b),UiText::lookup(fmt,wrap),ap);return b;
}
static void lines(const std::string&text,bool disabled=false) {
    if(disabled)ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    size_t start=0;
    do {
        size_t end=text.find('\n',start);if(end==std::string::npos)end=text.size();
        const char*a=text.data()+start,*b=text.data()+end;
        // Balanced layout indentation, NOT a cursor teleport after the item.
        // SetCursorPosX after the final text leaves IsSetPos pending at End().
        float offset=0;
        if(UiText::language==0) {
            float width=ImGui::CalcTextSize(a,b).x,available=ImGui::GetContentRegionAvail().x;
            if(available>width)offset=available-width;
        }
        if(offset>0)ImGui::Indent(offset);
        ImGui::TextUnformatted(a,b);
        if(offset>0)ImGui::Unindent(offset);
        if(end==text.size())break;start=end+1;
    } while(true);
    if(disabled)ImGui::PopStyleColor();
}
static void TextUnformatted(const char*s){lines(UiText::display(s));}
static void Text(const char*fmt,...){va_list ap;va_start(ap,fmt);auto t=format(fmt,ap,false);va_end(ap);lines(t);}
static void TextDisabled(const char*fmt,...){va_list ap;va_start(ap,fmt);auto t=format(fmt,ap,true);va_end(ap);lines(t,true);}
static void TextWrapped(const char*fmt,...){va_list ap;va_start(ap,fmt);auto t=format(fmt,ap,true);va_end(ap);
    if(UiText::language==0)lines(t);else ImGui::TextWrapped("%s",t.c_str());
}
static bool Button(const char*s,const ImVec2&size=ImVec2(0,0)){return ImGui::Button(UiText::label(s).c_str(),size);}
static bool Checkbox(const char*s,bool*v){return ImGui::Checkbox(UiText::label(s).c_str(),v);}
static bool SliderInt(const char*s,int*v,int min,int max){return ImGui::SliderInt(UiText::label(s).c_str(),v,min,max);}
static bool InputText(const char*s,char*b,size_t n,ImGuiInputTextFlags flags=0){return ImGui::InputText(UiText::label(s).c_str(),b,n,flags);}
static bool CollapsingHeader(const char*s,ImGuiTreeNodeFlags flags=0){return ImGui::CollapsingHeader(UiText::label(s).c_str(),flags);}
static bool BeginTabItem(const char*s,bool*open=nullptr,ImGuiTabItemFlags flags=0){return ImGui::BeginTabItem(UiText::label(s).c_str(),open,flags);}
static bool ColorPicker3(const char*s,float col[3],ImGuiColorEditFlags flags=0){return ImGui::ColorPicker3(UiText::label(s).c_str(),col,flags);}
static bool ColorEdit3(const char*s,float col[3],ImGuiColorEditFlags flags=0){return ImGui::ColorEdit3(UiText::label(s).c_str(),col,flags);}
static bool ColorButton(const char*desc_id,const ImVec4&col,ImGuiColorEditFlags flags=0,const ImVec2&size=ImVec2(0,0)){return ImGui::ColorButton(desc_id,col,flags,size);}
static bool Combo(const char*s,int*current_item,const char*const items[],int items_count,int height_in_items=-1){
    return ImGui::Combo(UiText::label(s).c_str(),current_item,items,items_count,height_in_items);
}
} // namespace Ui

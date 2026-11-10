// Exercise the production logLine body with mocked Win32 I/O; no game process.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
using DWORD=unsigned long;
static int logHandle=1;
static constexpr int INVALID_HANDLE_VALUE=-1;
static std::string output;
static int calls=0;
static DWORD GetTickCount(){return 0xffffffffUL;}
static DWORD GetCurrentThreadId(){return 0xffffffffUL;}
static bool WriteFile(int,const void *data,DWORD size,DWORD *written,void*){
    assert(size<600); // snprintf returns the UNTRUNCATED size; never pass that to I/O.
    output.assign(static_cast<const char*>(data),size);*written=size;++calls;return true;
}
#include "logging_under_test.h"
int main(){
    logLine("World timer status: OFF");
    assert(output=="[4294967295 tid=4294967295] World timer status: OFF\r\n" && calls==1);
    for(size_t n: {size_t(0),size_t(540),size_t(570),size_t(599),size_t(600),size_t(5000)}){
        std::string text(n,'x');logLine(text.c_str());
        assert(output.size()<600 && output.find('\0')==std::string::npos);
        assert(output.size()>=2 && output.substr(output.size()-2)=="\r\n");
        if(n>=599)assert(output.size()==599);
    }
    int before=calls;logHandle=INVALID_HANDLE_VALUE;logLine("ignored");assert(calls==before);
    puts("Production logger: bounded short/exact/oversized diagnostics, CRLF, no stack over-read, invalid handle passed. MOCK Win32 I/O.");
}

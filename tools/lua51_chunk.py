"""Portable Lua 5.1 chunk encoding for authored mod scripts (not game data).
Only little-endian, 32-bit ints/instructions, IEEE float/double are accepted.
The game uses float32 numbers and 32-bit size_t, unlike usual host Lua 5.1.
"""
import json
import math
import shutil
import struct
import subprocess

GAME_HEADER=b'\x1bLua\x51\0\1\4\4\4\4\0'
def validate(h):
    if len(h)!=12 or h[:8]!=GAME_HEADER[:8] or h[8] not in (4,8) or h[9]!=4 or h[10] not in (4,8) or h[11]!=0:
        raise ValueError('Expected supported little-endian Lua 5.1 chunk ABI')
class Reader:
    def __init__(self,data):
        self.data,self.pos,self.header=data,12,data[:12];validate(self.header)
    def take(self,n):
        if n<0 or self.pos+n>len(self.data):raise ValueError('Truncated Lua chunk')
        b=self.data[self.pos:self.pos+n];self.pos+=n;return b
    def u(self):return struct.unpack('<I',self.take(4))[0]
    def byte(self):return self.take(1)[0]
    def count(self):
        n=self.u()
        if n>1_000_000:raise ValueError('Unreasonable Lua vector length')
        return range(n)
    def string(self):
        n=struct.unpack('<I' if self.header[8]==4 else '<Q',self.take(self.header[8]))[0]
        if not n:return None
        s=self.take(n)
        if s[-1:]!=b'\0':raise ValueError('Nonterminated Lua string')
        return s[:-1]
    def proto(self,depth=0):
        if depth>100:raise ValueError('Lua nesting limit')
        p={'source':self.string(),'line':self.u(),'end':self.u()}
        for key in ('nups','params','vararg','stack'):p[key]=self.byte()
        p['code']=[self.u() for _ in self.count()];constants=[]
        for _ in self.count():
            tag=self.byte()
            if tag==0:value=None
            elif tag==1:
                value=self.byte()
                if value not in (0,1):raise ValueError('Invalid Lua boolean')
            elif tag==3:
                value=struct.unpack('<f' if self.header[10]==4 else '<d',self.take(self.header[10]))[0]
                if not math.isfinite(value):raise ValueError('Nonfinite Lua constant')
            elif tag==4:value=self.string()
            else:raise ValueError('Unknown Lua constant tag')
            constants.append((tag,value))
        p['constants']=constants;p['children']=[self.proto(depth+1) for _ in self.count()]
        p['lines']=[self.u() for _ in self.count()]
        p['locals']=[(self.string(),self.u(),self.u()) for _ in self.count()]
        p['names']=[self.string() for _ in self.count()]
        return p

def transcode(data,header):
    validate(header);r=Reader(data);p=r.proto()
    if r.pos!=len(data):raise ValueError('Trailing Lua chunk bytes')
    def u(n):return struct.pack('<I',n)
    def string(s):return (struct.pack('<I' if header[8]==4 else '<Q',len(s)+1)+s+b'\0') if s is not None else bytes(header[8])
    def vector(v,fn):return u(len(v))+b''.join(fn(x) for x in v)
    def constant(c):
        tag,value=c
        return bytes([tag])+(b'' if tag==0 else bytes([value]) if tag==1 else struct.pack('<f' if header[10]==4 else '<d',value) if tag==3 else string(value))
    def proto(x):
        return (string(x['source'])+u(x['line'])+u(x['end'])+bytes(x[k] for k in ('nups','params','vararg','stack'))+
            vector(x['code'],u)+vector(x['constants'],constant)+vector(x['children'],proto)+vector(x['lines'],u)+
            vector(x['locals'],lambda v:string(v[0])+u(v[1])+u(v[2]))+vector(x['names'],string))
    return header+proto(p)

def compile_script(source,name):
    lua=shutil.which('lua5.1')
    if not lua:raise RuntimeError('Build requires Lua 5.1 (lua5.1), not LuaJIT or a newer bytecode ABI')
    # Stable source name; no temporary paths or private game prototypes embedded.
    program='local f,e=loadstring(io.read("*a"),'+json.dumps('@mod/'+name)+'); assert(f,e); io.write(string.dump(f))'
    host=subprocess.run([lua,'-e',program],
                        input=source.encode(),capture_output=True,check=True,timeout=30).stdout
    return transcode(host,GAME_HEADER)

def include(name,chunk):
    # String literal supplies exactly one trailing NUL; consumers use sizeof-1.
    lines=['"'+''.join('\\x%02x'%b for b in chunk[i:i+24])+'"' for i in range(0,len(chunk),24)]
    return 'static constexpr char '+name+'[]=\n'+'\n'.join(lines)+';\n'

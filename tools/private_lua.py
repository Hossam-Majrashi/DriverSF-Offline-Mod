"""Optional read-only Lua 5.1 prototype fixture adapter for YOUR verified game.
No game code/data is included. Selected prototypes are emitted ONLY to a caller's
TemporaryDirectory, converting float32 constants/size_t for the host Lua 5.1 VM.
This tests native SCRIPT logic with mock APIs, not the native game runtime.
"""
from pathlib import Path
import hashlib
import re
import struct

SHA256='17fb9c7208f0b936d3977b04077f7cc49243b14fd3a0112d58f465c7984aa8c0'
GAME_HEADER=b'\x1bLua\x51\0\1\4\4\4\4\0'
class Reader:
    def __init__(self,data,pos):self.data,self.pos=data,pos
    def take(self,n):
        if n<0 or self.pos+n>len(self.data):raise ValueError('Truncated private Lua chunk')
        out=self.data[self.pos:self.pos+n];self.pos+=n;return out
    def u(self):return struct.unpack('<I',self.take(4))[0]
    def byte(self):return self.take(1)[0]
    def count(self):
        n=self.u()
        if n>1_000_000:raise ValueError('Unreasonable private chunk length')
        return range(n)
    def string(self):
        n=self.u()
        if not n:return None
        s=self.take(n)
        if not s.endswith(b'\0'):raise ValueError('Nonterminated private Lua string')
        return s[:-1]
    def proto(self,parent=None,depth=0):
        if depth>100:raise ValueError('Private prototype nesting limit')
        p={'source':self.string() or parent,'line':self.u(),'end':self.u()}
        p['nups'],p['params'],p['vararg'],p['stack']=[self.byte() for _ in range(4)]
        p['code']=[self.u() for _ in self.count()];constants=[]
        for _ in self.count():
            tag=self.byte()
            if tag==0:value=None
            elif tag==1:value=bool(self.byte())
            elif tag==3:value=struct.unpack('<f',self.take(4))[0]
            elif tag==4:value=self.string()
            else:raise ValueError('Unsupported private constant type')
            constants.append((tag,value))
        p['constants']=constants
        p['children']=[self.proto(p['source'],depth+1) for _ in self.count()]
        p['lines']=[self.u() for _ in self.count()]
        p['locals']=[(self.string(),self.u(),self.u()) for _ in self.count()]
        p['names']=[self.string() for _ in self.count()]
        return p

def all_protos(p):
    yield p
    for child in p['children']:yield from all_protos(child)

def selected(path,include_streets=False):
    data=Path(path).read_bytes()
    if hashlib.sha256(data).hexdigest()!=SHA256:raise ValueError('Unsupported private script-container hash')
    wanted={};last=0
    for m in re.finditer(re.escape(GAME_HEADER),data):
        if m.start()<last:continue
        reader=Reader(data,m.end());root=reader.proto();last=reader.pos
        for p in all_protos(root):
            source=(p['source'] or b'').lower()
            if p['line']==1790 and p['end']==1829 and source.endswith(b'\\goals\\goals.lua'):key='goal'
            elif p['line']==4592 and p['end']==4615 and source.endswith(b'\\goals\\goals.lua'):key='instance_goal'
            elif p['line']==3 and p['end']==168 and source.endswith(b'\\moodgroups.lua'):key='moods'
            elif include_streets and p['line']==394 and p['end']==408 and source.endswith(b'\\felony_patrollingvehiclemanager.lua'):key='patrol'
            elif include_streets and p['line']==410 and p['end']==414 and source.endswith(b'\\felony_patrollingvehiclemanager.lua'):key='patrol_spawn'
            elif include_streets and p['line']==31 and p['end']==42 and source.endswith(b'\\trafficdata.lua'):key='road'
            elif include_streets and p['line']==1634 and p['end']==1636 and source.endswith(b'\\localplayer.lua'):key='player_task'
            else:continue
            if key in wanted or p['nups']:raise ValueError('Ambiguous/upvalued private factory')
            wanted[key]=p
    expected={'goal','instance_goal','moods'}|({'patrol','patrol_spawn','road','player_task'} if include_streets else set())
    if set(wanted)!=expected:raise ValueError('Supported private prototypes missing')
    return wanted

def selected_paranoia(common,chapter):
    """Read-only fixtures for the buffered HUD and actual suspicion callback."""
    wanted={}
    specs={
        'goal':(b'\\goals\\goals.lua',1790,1829,0),
        'hud':(b'\\feedbacksystemfunctions.lua',921,1043,21),
        'callback':(b'\\feedbacksystemfunctions.lua',1101,1103,2),
        'payload':(b'\\tasks\\payloadtracking.lua',15,42,0),
        'owner':(b'\\hud\\thesneakouthud.lua',139,167,6),
    }
    for path,sha in ((common,SHA256),(chapter,'a4c2156df92a6df1a1a37706fff3707d8dbb27dc94165e9bd1f34de059cfb710')):
        data=Path(path).read_bytes();last=0
        if hashlib.sha256(data).hexdigest()!=sha:raise ValueError('Unsupported private timer container hash')
        for m in re.finditer(re.escape(GAME_HEADER),data):
            if m.start()<last:continue
            reader=Reader(data,m.end());root=reader.proto();last=reader.pos
            for p in all_protos(root):
                for key,(suffix,first,end,nups) in specs.items():
                    if (p['source'] or b'').lower().endswith(suffix) and (p['line'],p['end'])==(first,end):
                        if key in wanted or p['nups']!=nups:raise ValueError('Ambiguous private timer prototype')
                        wanted[key]=p
    if set(wanted)!=set(specs):raise ValueError('Private timer prototypes missing')
    return wanted

def dump(p,host_header):
    if host_header[:8]!=b'\x1bLua\x51\0\1\4' or host_header[8] not in (4,8) or host_header[9:]!=bytes((4,8,0)):
        raise ValueError('Expected standard little-endian Lua 5.1 host with double numbers')
    def u(n):return struct.pack('<I',n)
    def string(s):return (struct.pack('<I' if host_header[8]==4 else '<Q',len(s)+1)+s+b'\0') if s is not None else bytes(host_header[8])
    def vector(v,fn):return u(len(v))+b''.join(fn(x) for x in v)
    def constant(c):
        tag,value=c
        return bytes([tag])+(b'' if tag==0 else bytes([value]) if tag==1 else struct.pack('<d',value) if tag==3 else string(value))
    def proto(x):
        return (string(x['source'])+u(x['line'])+u(x['end'])+bytes(x[k] for k in ('nups','params','vararg','stack'))+
            vector(x['code'],u)+vector(x['constants'],constant)+vector(x['children'],proto)+vector(x['lines'],u)+
            vector(x['locals'],lambda v:string(v[0])+u(v[1])+u(v[2]))+vector(x['names'],string))
    return host_header+proto(p)

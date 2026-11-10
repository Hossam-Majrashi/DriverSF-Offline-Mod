#!/usr/bin/env python3
"""Reversible, fixed-length visual-only DSF archive patch. No executable hooks.
prepare requires the extracted inventory and numpy. apply/restore/verify use stdlib.
The game must be closed. Never removes character logic, collision or audio.
"""
from pathlib import Path
from dataclasses import dataclass
import argparse, collections, contextlib, hashlib, json, mmap, os, re, shutil, struct, sys

HERE=Path(__file__).resolve().parent
GAME=HERE.parent.parent
PLAN=HERE/'plan.json'
BACKUP=GAME/'mod/backups/asset_filter_v1/original'


def require(ok,message):
    if not ok:raise ValueError(message)


def sha(b):return hashlib.sha256(b).hexdigest()


def digest(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(4<<20),b''):h.update(b)
    return h.hexdigest()


def save_json(p,data):
    p.parent.mkdir(parents=True,exist_ok=True)
    tmp=p.with_name(p.name+'.tmp')
    with tmp.open('w',encoding='utf8') as f:
        json.dump(data,f,ensure_ascii=False,indent=2);f.write('\n');f.flush();os.fsync(f.fileno())
    os.replace(tmp,p)


def safe_game_path(relative):
    p=(GAME/relative).resolve()
    require(GAME in p.parents and '..' not in Path(relative).parts,'unsafe archive path')
    return p


def ensure_closed():
    # Linux /proc comm is separate from command arguments containing our game path.
    for p in Path('/proc').glob('[0-9]*/comm'):
        try:name=p.read_text().strip().lower()
        except (OSError,UnicodeError):continue
        require(name not in ('driver.exe','driver'),f'Close the game first (PID {p.parent.name})')


@dataclass
class Node:
    tag:str
    offset:int
    size:int
    version:int
    children:list


def parse_tree(b,base=0,end=None,depth=0):
    end=len(b) if end is None else end
    require(depth<25 and base+16<=end,'invalid CHNK depth/header')
    magic,total,n,ver=struct.unpack_from('<4sIII',b,base)
    require(magic==b'CHNK' and total>=16 and base+total<=end,'invalid CHNK size/magic')
    require(not(ver&0x80000000),'compressed CHNK unsupported')
    require(n<=1000000 and 16+n*16<=total,'invalid CHNK table')
    result=[]
    for i in range(n):
        tag,off,version,nlen,align,res,size=struct.unpack_from('<4sIBBBBI',b,base+16+i*16)
        pos=base+off
        require(off>=16+n*16 and off+size+nlen<=total,'CHNK child outside parent')
        nested=parse_tree(b,pos,pos+size,depth+1) if size>=16 and b[pos:pos+4]==b'CHNK' else []
        result.append(Node(tag.decode('ascii'),pos,size,version,nested))
    return result


def walk(nodes):
    for n in nodes:
        yield n
        yield from walk(n.children)


def child(n,tag):
    matches=[x for x in n.children if x.tag==tag]
    require(len(matches)==1,f'{n.tag}: expected one {tag}')
    return matches[0]


def data(b,n):return b[n.offset:n.offset+n.size]


def tree_signature(nodes):
    return [(n.tag,n.offset,n.size,n.version,tree_signature(n.children)) for n in nodes]


def neutralize_triangles(raw,index_count,ranges,protected_ranges):
    import numpy as np
    require(len(raw)==index_count*2,'unexpected index buffer length')
    before=np.frombuffer(raw,dtype='<u2');after=before.copy();mask=np.zeros(index_count,dtype=bool)
    for first,count in ranges:
        require(first>=0 and count>0 and count%3==0 and first+count<=index_count,'invalid selected triangle span')
        mask[first:first+count]=True
        tris=after[first:first+count].reshape(-1,3)
        tris[:,1]=tris[:,0];tris[:,2]=tris[:,0]
    for first,count in protected_ranges:
        require(first>=0 and count>=0 and first+count<=index_count,'invalid protected index span')
        require(not mask[first:first+count].any(),'selected triangles overlap a non-selected submodel')
    require(np.array_equal(before[~mask],after[~mask]),'modified non-selected indices')
    for first,count in ranges:
        t=after[first:first+count].reshape(-1,3)
        require(bool((t[:,0]==t[:,1]).all() and (t[:,1]==t[:,2]).all()),'visible triangles remain')
    return after.tobytes(),int(mask.sum()//3)


def female_material(path):
    basename=path.lower().replace('\\','/').rsplit('/',1)[-1]
    return '_gf.' in basename or bool(re.match(r'^f(?:as|bk|ch|la|mi|ot|wt)\d',basename))


def prepare(manifest_path):
    require(not PLAN.exists(),'plan already exists; use verify/apply/restore')
    require(not BACKUP.exists(),'backup directory already exists; inspect it before preparing a new plan')
    ensure_closed()
    inventory=json.loads(manifest_path.read_text());selection=json.loads((HERE/'selection.json').read_text())
    assets=manifest_path.parent;images=inventory['images'];models=inventory['models']
    expected={s['source']:s['sha256_before'] for s in inventory['sources']}
    operations=[];image_log=[];model_log=[]
    def add(source,offset,before,after,kind,details):
        require(len(before)==len(after) and len(before)>0,'replacement changes archive length')
        require(before!=after,'replacement is a no-op')
        k=len(operations);path=HERE/'payloads'/f'{k:04d}.bin';path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(after)
        operations.append({'source':source,'offset':offset,'bytes':len(before),'before_sha256':sha(before),'after_sha256':sha(after),'payload':str(path.relative_to(HERE)),'kind':kind,'details':details})
    def image_at(src,index):
        rr=[r for r in images if r['source']==src and Path(r['file']).name.startswith(f'{index:04d}_')]
        require(len(rr)==1,f'cannot locate image donor {src}:{index}')
        return rr[0]
    selected_images=[]
    for name in selection['image_ids']:
        rr=[r for r in images if Path(r['file']).stem==name]
        require(len(rr)==1,f'unresolved explicit image: {name}')
        selected_images.append(rr[0])
    explicit_image_count=len(selected_images)
    reviewed={(r['source'],r['index']):r for r in selection.get('additional_reviewed_images',[])}
    selected_images.extend(image_at(src,index) for src,index in reviewed)
    patched=set()
    for r in selected_images:
        index=int(Path(r['file']).name[:4]);src=r['source']
        if (src,index) in reviewed:donor_index=reviewed[(src,index)]['donor_index']
        elif src=='Install/san_francisco.dngc':donor_index=42
        elif src=='Install/EBoard.sp':donor_index={9:10,15:16,21:22,31:27,33:28}[index]
        elif src=='Install/gui.sp':donor_index=871 if index>=870 else 244 if 255<=index<=266 or index in (168,169) else 150
        elif src=='media/GUI_FrontEnd.fchunk':donor_index=14
        else:raise ValueError(f'no donor for {src}')
        donor=image_at(src,donor_index);before=(assets/r['file']).read_bytes();after=(assets/donor['file']).read_bytes()
        require(sha(before)==r['sha256'] and sha(after)==donor['sha256'],'extracted texture hash mismatch')
        require(before[:128]==after[:128] and len(before)==len(after),'DDS dimensions/format/mip layout differs')
        copies=[x for x in images if x['kind']=='DDS' and (x['sha256']==r['sha256'] or x['uid']==r['uid'])]
        for copy in copies:
            key=(copy['source'],copy['offset'])
            if key in patched:continue
            b=(assets/copy['file']).read_bytes()
            require(b[:128]==after[:128] and len(b)==len(after),'duplicate UID has incompatible image layout')
            add(copy['source'],copy['offset'],b,after,'dds_replace',{'requested_id':Path(r['file']).stem,'target_uid':copy['uid'],'donor_file':donor['file'],'donor_uid':donor['uid'],'header_preserved':True})
            patched.add(key);image_log.append({'requested':Path(r['file']).stem,'target':copy['file'],'donor':donor['file']})
    explicit_packages=set(selection['explicit_challenges_packages'])
    named_packages={r['package_index'] for r in models if r['source']=='Install/Challenges.sp' and any(female_material(p) for p in r['material_paths'])} if selection['include_other_named_female_packages'] else set()
    challenge_packages=explicit_packages|named_packages
    global_parts=set(selection['global_parts']);split_parts=set(selection['globalsplit_parts'])
    selected_part_uids={r['part_uid'] for r in models if (r['source']=='Install/character_global.fchunk' and r['part_index'] in global_parts) or (r['source']=='Install/character_globalsplit.fchunk' and r['part_index'] in split_parts)}
    chosen=[r for r in models if (r['source']=='Install/Challenges.sp' and r['package_index'] in challenge_packages) or ('character_global' in r['source'] and r['part_uid'] in selected_part_uids)]
    require(all(any(r['package_index']==p and r['source']=='Install/Challenges.sp' for r in chosen) for p in explicit_packages),'missing explicit character package')
    for src in ['Install/Challenges.sp','Install/character_global.fchunk','Install/character_globalsplit.fchunk']:
        p=GAME/src
        with p.open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as b:
            roots=parse_tree(b)
            for package_index,package in enumerate(roots):
                selected=[r for r in chosen if r['source']==src and r['package_index']==package_index]
                if not selected:continue
                required={uid for r in selected for uid in r['submodel_uids']}
                all_entries=list(walk([package]));subs={};ibs={}
                for e in all_entries:
                    if e.tag=='SBBH':
                        h=data(b,e);require(e.size==72 and e.version==4,'unsupported SBBH layout')
                        require(h[:8].hex() not in subs,'duplicate SBBH UID in package')
                        subs[h[:8].hex()]={'ib':h[48:56].hex(),'first':struct.unpack_from('<I',h,56)[0],'count':struct.unpack_from('<I',h,60)[0]}
                    elif e.tag=='IXCK':
                        h=data(b,child(e,'IXHD'));count,size=struct.unpack_from('<II',h,8)
                        require(size==2,'only 16-bit index buffers supported')
                        ibs[h[:8].hex()]=(count,child(e,'IXDT'))
                require(required<=subs.keys(),'unresolved selected submodel UID')
                for ib_uid,(count,entry) in ibs.items():
                    ranges=sorted({(s['first'],s['count']) for uid,s in subs.items() if uid in required and s['ib']==ib_uid and s['count']})
                    if not ranges:continue
                    protected=[(s['first'],s['count']) for uid,s in subs.items() if uid not in required and s['ib']==ib_uid]
                    before=data(b,entry);after,n=neutralize_triangles(before,count,ranges,protected)
                    add(src,entry.offset,before,after,'degenerate_selected_triangles',{'package_index':package_index,'index_buffer_uid':ib_uid,'index_count':count,'selected_ranges':ranges,'protected_ranges':protected,'triangles_hidden':n,'submodel_uids':sorted(uid for uid in required if subs[uid]['ib']==ib_uid)})
                for r in selected:model_log.append({k:r[k] for k in ('label','source','package_index','part_index','part_uid','mask','submodel_uids')})
    by_source=collections.defaultdict(list)
    for op in operations:by_source[op['source']].append(op)
    files=[]
    for src,ops in sorted(by_source.items()):
        p=safe_game_path(src)
        require(digest(p)==expected[src],f'{src} changed since extraction; do not patch unknown offsets')
        ops.sort(key=lambda x:x['offset']);end=0
        for op in ops:
            require(op['offset']>=end and op['offset']+op['bytes']<=p.stat().st_size,'patch ranges overlap or exceed file')
            end=op['offset']+op['bytes']
        backup=BACKUP/src;backup.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,backup)
        require(digest(backup)==expected[src],'backup verification failed')
        staged=HERE/'prepared'/src;staged.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(backup,staged)
        write_ops(staged,ops)
        before_signature=None
        with backup.open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as b:before_signature=tree_signature(parse_tree(b))
        with staged.open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as b:
            require(tree_signature(parse_tree(b))==before_signature,'CHNK structure changed')
        files.append({'source':src,'bytes':p.stat().st_size,'before_sha256':expected[src],'after_sha256':digest(staged),'backup':str(backup.relative_to(GAME)),'prepared':str(staged.relative_to(HERE))})
    plan={'version':1,'game_path':str(GAME),'method':'Native DDS replacement; selected triangle index triplets collapse to their first vertex, preserving index validity, counts, buffers, archive tables and character mission data. No male mesh retargeting.','files':files,'operations':operations,'images':image_log,'models':model_log,'summary':{'explicit_images':explicit_image_count,'additional_reviewed_images':len(reviewed),'image_occurrences_replaced':len(image_log),'explicit_challenge_packages':len(explicit_packages),'additional_named_female_packages':sorted(named_packages-explicit_packages),'total_challenge_packages':len(challenge_packages),'global_part_uids':len(selected_part_uids),'model_mask_variants_hidden':len(model_log),'submodels_hidden':sum(len(op['details']['submodel_uids']) for op in operations if op['kind']=='degenerate_selected_triangles'),'triangles_hidden':sum(op['details']['triangles_hidden'] for op in operations if op['kind']=='degenerate_selected_triangles'),'archives':len(files)},'limitations':['Changes are on disk and apply regardless of whether dinput8.dll is loaded, until restore is run.','Visual hiding only: female voices, mission logic, collision, animations and BIK/cutscene videos are not removed.','No claim that unselected/unclassified images or all possible female depictions were found.','Runtime appearance, mission completion and renderer behavior require in-game testing.']}
    save_json(PLAN,plan);save_json(HERE/'state.json',{'mode':'prepared_not_applied'})
    print(json.dumps(plan['summary'],indent=2))
    print('Prepared and backed up; game files have not been modified yet.')


def write_ops(path,operations):
    with path.open('r+b') as f:
        for op in operations:
            f.seek(op['offset']);before=f.read(op['bytes'])
            require(sha(before)==op['before_sha256'],'patch preimage mismatch')
            after=(HERE/op['payload']).read_bytes()
            require(len(after)==op['bytes'] and sha(after)==op['after_sha256'],'patch payload mismatch')
            f.seek(op['offset']);f.write(after)
        f.flush();os.fsync(f.fileno())


def inspect_state(plan):
    rows=[]
    for row in plan['files']:
        p=safe_game_path(row['source']);h=digest(p)
        state='original' if h==row['before_sha256'] else 'filtered' if h==row['after_sha256'] else 'UNKNOWN_MODIFICATION'
        rows.append({'source':row['source'],'state':state,'sha256':h})
    return rows


def apply_or_restore(plan,restoring=False):
    ensure_closed();rows=inspect_state(plan);states={r['state'] for r in rows}
    target='original' if restoring else 'filtered'
    if states=={target}:print(f'Already {target}; no changes needed.');return
    require(states==({'filtered'} if restoring else {'original'}),'Mixed or unknown archive hashes; refusing to overwrite other changes. See verify.')
    stage_files=[];committed=[]
    try:
        for row in plan['files']:
            p=safe_game_path(row['source']);backup=GAME/row['backup']
            require(digest(backup)==row['before_sha256'],'backup has changed')
            stage=p.with_name(p.name+'.asset-filter-staging')
            require(not stage.exists(),f'leftover staging file: {stage}')
            stage_files.append(stage);shutil.copy2(backup,stage)
            if not restoring:write_ops(stage,[op for op in plan['operations'] if op['source']==row['source']])
            require(digest(stage)==row['before_sha256' if restoring else 'after_sha256'],'staging hash mismatch')
        ensure_closed()
        for row,stage in zip(plan['files'],stage_files):
            p=safe_game_path(row['source'])
            require(digest(p)==row['after_sha256' if restoring else 'before_sha256'],'source changed during staging')
            os.replace(stage,p);committed.append(row)
            fd=os.open(str(p.parent),os.O_RDONLY)
            try:os.fsync(fd)
            finally:os.close(fd)
        require(all(r['state']==target for r in inspect_state(plan)),'post-install verification failed')
        save_json(HERE/'state.json',{'mode':target,'all_expected_hashes_verified':True,'runtime_tested':False})
        print(f'{target}: {len(committed)} archives, all hashes verified. Runtime NOT tested.')
    except BaseException:
        # Roll back complete files, never leave a partly overwritten archive.
        for row in reversed(committed):
            p=safe_game_path(row['source']);rollback=p.with_name(p.name+'.asset-filter-rollback')
            origin=(HERE/row['prepared']) if restoring else (GAME/row['backup'])
            shutil.copy2(origin,rollback);os.replace(rollback,p)
        raise
    finally:
        for p in stage_files:
            if p.exists():p.unlink()


def verify(plan):
    rows=inspect_state(plan);details=[]
    for row in rows:
        require(row['state']!='UNKNOWN_MODIFICATION',f'Unknown changes: {row["source"]}')
        p=GAME/row['source']
        with p.open('rb') as f:
            for op in [o for o in plan['operations'] if o['source']==row['source']]:
                f.seek(op['offset']);b=f.read(op['bytes']);expected=op['after_sha256' if row['state']=='filtered' else 'before_sha256']
                require(sha(b)==expected,'operation range verification failed')
                if row['state']=='filtered' and op['kind']=='degenerate_selected_triangles':
                    for first,count in op['details']['selected_ranges']:
                        for offset in range(first*2,(first+count)*2,6):
                            a,c,d=struct.unpack_from('<3H',b,offset)
                            require(a==c==d,'non-degenerate selected triangle remains')
                if op['kind']=='dds_replace':require(b[:4]==b'DDS ' and struct.unpack_from('<I',b,4)[0]==124,'invalid DDS header')
                details.append({'source':row['source'],'offset':op['offset'],'kind':op['kind'],'verified':True})
    report={'files':rows,'verified_operation_ranges':len(details),'summary':plan['summary'],'runtime_tested':False,'logic_or_video_removed':False}
    save_json(HERE/'verification.json',report);print(json.dumps(report,indent=2))


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('command',choices=['prepare','apply','restore','verify']);ap.add_argument('--manifest',type=Path,default=GAME.parent/'DSF_Assets/manifest.json');args=ap.parse_args()
    import fcntl
    HERE.mkdir(parents=True,exist_ok=True)
    with (HERE/'.lock').open('w') as lock:
        fcntl.flock(lock.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB)
        if args.command=='prepare':prepare(args.manifest.resolve());return
        require(PLAN.is_file(),'Run prepare first');plan=json.loads(PLAN.read_text())
        if args.command=='verify':verify(plan)
        else:apply_or_restore(plan,args.command=='restore')


if __name__=='__main__':
    try:main()
    except Exception as ex:print(f'ERROR: {ex}',file=sys.stderr);sys.exit(1)

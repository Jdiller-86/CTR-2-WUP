#!/usr/bin/env python3
"""Review one FTP return folder or ZIP; preserve partial/failed attempts separately."""
import argparse
import hashlib
import json
from pathlib import Path,PurePosixPath
import re
import stat
import zipfile
MAX_FILE=8*1024*1024
MAX_TOTAL=48*1024*1024


def object_pairs(pairs):
    out={}
    for k,v in pairs:
        if k in out: raise ValueError('Duplicate JSON field: '+k)
        out[k]=v
    return out


def inventory(path):
    result={};total=0
    def add(name,data):
        nonlocal total
        p=PurePosixPath(name)
        if p.is_absolute() or '..' in p.parts or '\\' in name or name in result: raise ValueError('Unsafe/duplicate return path')
        total+=len(data)
        if len(data)>MAX_FILE or total>MAX_TOTAL or len(result)>=512: raise ValueError('Return exceeds bounded limits')
        result[name]=data
    if path.is_dir():
        if any(p.is_symlink() for p in (path,*path.parents)): raise ValueError('Symlink return root')
        for p in sorted(path.rglob('*')):
            if p.is_symlink(): raise ValueError('Symlink return entry')
            if p.is_file():
                if p.stat().st_size>MAX_FILE: raise ValueError('Oversized return file')
                add(p.relative_to(path).as_posix(),p.read_bytes())
    else:
        with zipfile.ZipFile(path) as z:
            for p in z.infolist():
                if p.is_dir(): continue
                if p.file_size>MAX_FILE or stat.S_ISLNK(p.external_attr>>16): raise ValueError('Oversized/symlink ZIP member')
                add(p.filename,z.read(p))
    return result


def review(path,manifest):
    if manifest.get('kind')!='ctr2wup-runtime-round' or not re.fullmatch('[0-9a-f]{40}',manifest.get('source_commit','')): raise ValueError('Require the delivered runtime-round manifest')
    files=inventory(path);groups={};errors=[]
    for name,data in files.items():
        if not PurePosixPath(name).name.endswith('.txt'): continue
        is_result=PurePosixPath(name).name.startswith('result-guest-dsp-')
        for line in data.decode('utf-8',errors='replace').splitlines():
            if not line.startswith('DSP_JSON '): continue
            try:
                row=json.loads(line[9:],object_pairs_hook=object_pairs)
                if (not isinstance(row,dict) or row.get('kind')!='ctr2wup-guest-dsp' or type(row.get('schema')) is not int or row['schema']!=1
                    or not re.fullmatch('[0-9a-f]{16}',row.get('session',''))): raise ValueError('Invalid DSP record identity')
                key=(row.get('source_commit'),row['session'])
                group=groups.setdefault(key,{'trace':[],'result':[]})
                group['result' if is_result else 'trace'].append((name,row))
            except (ValueError,TypeError) as error: errors.append({'file':name,'error':str(error)})
    attempts=[]
    for (source,session),group in sorted(groups.items()):
        trace=group['trace'];result=group['result'];problems=[]
        if source!=manifest['source_commit']: problems.append('different_source')
        if any(type(r.get('source_dirty')) is not int or r['source_dirty']!=0 for _,r in trace+result): problems.append('dirty_or_unknown_source')
        if len(result)!=1 or len({n for n,_ in trace})!=1: problems.append('missing_or_duplicate_report_pair')
        records=[r for _,r in trace]
        sequences=[r.get('sequence') for r in records]
        if not sequences or any(type(n) is not int for n in sequences) or sequences!=list(range(1,len(sequences)+1)): problems.append('incomplete_or_reordered_trace')
        last=result[0][1] if len(result)==1 else {}
        if not records or last!=records[-1]: problems.append('trace_result_mismatch')
        stages=[r.get('stage') for r in records]
        prefix=['startup','retained_logs_collected','guest_rendered']
        suffix=['hearing_recorded','resources_closed']
        middle=stages[3:-2]
        valid=stages[:3]==prefix and stages[-2:]==suffix and 4<=len(middle)<=32 and len(middle)%4==0
        for i in range(0,len(middle),4):
            valid=valid and middle[i:i+4]==['playing' if i==0 else 'replay','paused','resumed','playback_complete']
        if not valid: problems.append('incomplete_or_reordered_attempt')
        if any(r.get('failed') is not False for r in records): problems.append('recorded_failure')
        for key,value in {'core_ok':True,'pause':True,'resume':True,'pause_cursor_held':True,'native_complete':True,
                          'ax_owned':False,'finite_prerender':True,'installed_exit_tested':False}.items():
            if last.get(key) is not value: problems.append(key)
        expected={'dsp_blocks':300,'dsp_samples':48000,'queued':2,'loops':4,'output_frames':70398,
                  'arm_instructions':manifest['reference']['arm_instructions'],'native_failures':0,'native_releases':1,'native_result':0}
        for key,value in expected.items():
            if type(last.get(key)) is not int or last[key]!=value: problems.append(key)
        if last.get('cursor')!=[70398,70398] or any(type(x) is not int for x in last.get('cursor',[])): problems.append('cursor')
        if last.get('pcm_sha256')!=manifest['reference']['pcm_sha256']: problems.append('pcm_sha256')
        automatic='pass' if not problems else 'fail_or_incomplete'
        human=last.get('hearing','pending')
        attempts.append({'source_commit':source,'session':session,'automatic':automatic,'hearing':human,
                         'accepted_finite_audio':automatic=='pass' and human=='pass','problems':sorted(set(problems)),
                         'system_logs':{'present':last.get('system_logs_present'),'complete':last.get('system_logs_complete'),
                                        'files':last.get('system_log_files'),'error':last.get('system_logs_error')}})
    candidates=[]
    for name,data in sorted(files.items()):
        if not any(part.startswith('system-logs-') for part in PurePosixPath(name).parts): continue
        hits=[]
        for match in re.finditer(rb'(?i)procui|gx2|osfatal|rpl|loader|sysapp|deadlock|exception|assert',data):
            hits.append({'offset':match.start(),'term':match.group().decode('ascii').lower()})
            if len(hits)==32:break
        candidates.append({'path':name,'bytes':len(data),'diagnostic_terms':hits})
    receipts=[{'path':n,'bytes':len(d),'sha256':hashlib.sha256(d).hexdigest()} for n,d in sorted(files.items())]
    return {'schema':1,'kind':'ctr2wup-runtime-review','source_commit':manifest['source_commit'],
            'attempts':attempts,'parse_errors':errors,'files':receipts,'system_log_candidates':candidates,'raw_system_logs_private':True,
            'installed_exit':'confirmed_failed_pending_system_log_diagnosis',
            'full_guest_audio_milestone_accepted':False,'console_return_requires_observation':True,
            'ok':any(a['source_commit']==manifest['source_commit'] for a in attempts) and not errors and all(a['accepted_finite_audio'] for a in attempts if a['source_commit']==manifest['source_commit'])}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',type=Path,required=True)
    p.add_argument('--manifest',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    result=review(a.input,json.loads(a.manifest.read_text()));a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'ok':result['ok'],'attempts':len(result['attempts']),'parse_errors':len(result['parse_errors'])}))

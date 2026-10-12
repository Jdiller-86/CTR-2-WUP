#!/usr/bin/env python3
"""Idempotently synchronize seven roadmap issues from checked-in evidence.

Dry-run by default. --apply creates missing labels/milestones/tracker issues and
updates only their managed body section. Never closes acceptance gates, erases
comments, creates a paid service or changes repository visibility/settings.
Uses normal gh authentication; tokens never enter arguments or reports.
"""
import argparse
import json
import re
import subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BEGIN='<!-- ctr2wup:managed:start -->'
END='<!-- ctr2wup:managed:end -->'
COLORS={'milestone':'5319e7','console-testing':'1d76db','needs-console':'fbca04','needs-input':'d93f0b','needs-windows':'c2e0c6',
        'area:cpu':'1d76db','area:runtime':'0052cc','area:graphics':'bfd4f2','area:host':'0e8a16'}

def api(path,method='GET',payload=None):
    command=['gh','api',path,'--method',method]
    if payload is not None:command+=['--input','-']
    result=subprocess.run(command,input=json.dumps(payload) if payload is not None else None,
                          capture_output=True,text=True,encoding='utf-8')
    if result.returncode:raise RuntimeError('GitHub '+method+' '+path+' failed: '+result.stderr.strip())
    return json.loads(result.stdout) if result.stdout.strip() else None

def pages(path):
    items=[]
    for page in range(1,11):
        data=api(path+('&' if '?' in path else '?')+'per_page=100&page='+str(page))
        if not isinstance(data,list):raise RuntimeError('Unexpected GitHub list response')
        items.extend(data)
        if len(data)<100:return items
    raise RuntimeError('GitHub list exceeds supported 1,000-entry budget')

def body(repo,state,m):
    base='https://github.com/'+repo;marker='<!-- ctr2wup:milestone:'+m['id']+' -->'
    content=[BEGIN,'## Current evidence','',m['summary'],'',
             'Updated '+state['updated']+' for **v'+state['version']+'**. Approximately **'+str(state['approximate_implementation_percent'])+
             '% implementation**, **'+str(state['full_milestones_accepted'])+'/7 full gates accepted**; these are separate measures.','',
             '### Implemented foundations','']
    content+=['- [x] '+item for item in m['verified_foundations']]
    content+=['','### Remaining acceptance','']+['- [ ] '+item for item in m['acceptance_remaining']]
    content+=['','[Current status]('+base+'/blob/main/STATUS.md) · [Acceptance checklist]('+base+'/blob/main/docs/milestones.md) · '
              '[Documentation]('+base+'/blob/main/docs/README.md)','',
              'The section above is generated from versioned progress files. Record new evidence in comments; a maintainer then updates the source assessment. '
              'Desktop builds and CI never establish console/game acceptance. Do not attach ROMs, keys, saves or extracted commercial assets.',END]
    return marker+'\n\n'+'\n'.join(content)+'\n\n## Maintainer notes\n\n'

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--repo',default='Jdiller-86/CTR-2-WUP')
    p.add_argument('--apply',action='store_true');p.add_argument('--output',type=Path);a=p.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+',a.repo):raise ValueError('Require owner/repository')
    state=json.loads((ROOT/'docs/milestone-status.json').read_text());milestones=state['milestones']
    if state['schema_version']!=1 or [m['id'] for m in milestones]!=['m'+str(i) for i in range(1,8)] or type(state['full_milestones_accepted']) is not int or not 0<=state['full_milestones_accepted']<=7:
        raise ValueError('Review tracker schema/acceptance policy before synchronization')
    prefix='repos/'+a.repo;labels={item['name'] for item in pages(prefix+'/labels')}
    remote={item['title']:item for item in pages(prefix+'/milestones?state=all')}
    issues=pages(prefix+'/issues?state=all');operations=[]
    def mutate(kind,path,payload,method='POST'):
        operations.append({'kind':kind,'method':method,'path':path})
        return api(path,method,payload) if a.apply else None
    for name,color in COLORS.items():
        if name not in labels:mutate('label',prefix+'/labels',{'name':name,'color':color,'description':'CTR2WUP '+name+' tracking'})
    for m in milestones:
        milestone=remote.get(m['title'])
        if milestone is None:milestone=mutate('milestone',prefix+'/milestones',{'title':m['title'],'description':m['summary']})
        marker='<!-- ctr2wup:milestone:'+m['id']+' -->';existing=[i for i in issues if marker in (i.get('body') or '') and 'pull_request' not in i]
        if len(existing)>1:raise RuntimeError('Duplicate managed tracker '+m['id'])
        generated=body(a.repo,state,m)
        if existing:
            issue=existing[0];old=issue.get('body') or ''
            if old.count(BEGIN)!=1 or old.count(END)!=1:raise RuntimeError('Managed issue markers changed; preserve and review '+str(issue['number']))
            replacement=generated[generated.index(BEGIN):generated.index(END)+len(END)]
            new=old[:old.index(BEGIN)]+replacement+old[old.index(END)+len(END):]
            if new!=old:mutate('issue-update',prefix+'/issues/'+str(issue['number']),{'body':new},'PATCH')
            operations.append({'kind':'tracker','id':m['id'],'number':issue['number'],'url':issue['html_url'],'existing':True})
        else:
            payload={'title':m['title'],'body':generated,'labels':['milestone',m['area'],*m['waiting_labels']]}
            if milestone is not None:payload['milestone']=milestone['number']
            issue=mutate('issue',prefix+'/issues',payload)
            if issue:operations.append({'kind':'tracker','id':m['id'],'number':issue['number'],'url':issue['html_url'],'existing':False})
    report={'ok':True,'applied':a.apply,'version':state['version'],'repository':a.repo,'operations':operations}
    if a.output:a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))
if __name__=='__main__':main()

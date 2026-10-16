#!/usr/bin/env python3
"""Restore the detailed native work breakdown retired by PR #122.

Requirements come from an immutable public source revision, not a new editable
catalog. Existing issue identities, accepted evidence and implementation links
are retained. Preview is read-only; apply requires the exact trusted main.
"""
import base64
import hashlib
import json
from pathlib import Path
import re

import project

HISTORY = '8da7a4a2cde0e6f80f7df5c6ec263efb4c98b702'
HISTORY_BLOBS = {
    'milestone-status.json': '2c5ac25c7ad72d3fe0d938965e11c9783902d38c',
    'features.json': 'c11270a7eaaf8053087849372bed587faf6227f5',
}
BASE = 'https://github.com/' + project.REPOSITORY
PREFIX = 'Consolidated into the milestone overview and '
BANNER = (PREFIX + '[development plan](' + BASE + '/blob/main/plans.md). '
    'Closing this planning checklist as superseded, not as completed or accepted. '
    'The original scope and evidence remain below.\n\n---\n\n')
REOPEN = {13,15,16,17,18,19,20,21,22,23,24,26,28,29,30,31,32,33,34,35,36,38,39,40,41,42,43,44,45,46,77,78,79,81}
# Last reviewed pre-consolidation statuses, with full-pipeline readiness retained.
STATUSES = {13:'Awaiting evidence',15:'Awaiting evidence',16:'Awaiting evidence',
    **{n:'In progress' for n in (17,18,19,23,28,29,30,32,33,39)},
    **{n:'Awaiting evidence' for n in (20,21,22,26,31)},
    **{n:'Blocked' for n in (34,35,36,38,40,41)},
    **{n:'Planned' for n in (24,42,43,44,45,46,77,78,79,81)}}
PARENTS = {n:1+(n-12)//5 for n in range(12,47)} | {77:29,78:29,79:30,80:28,81:31}
TASK_IDS = {25:'m3-guest-audio',37:'m6-original-wup'}
COMPACTED_PROJECT = {
    'shortDescription': 'Milestones and active development.',
    'readme': ('Milestones and active development. [Development plan](' + BASE + '/blob/main/plans.md).\n\n'
        'Closed work and pull requests are archived, with history retained. '
        'A task finishing never accepts a full milestone. Existing hardware '
        'evidence is reused; request only a changed path or missing criterion.'),
}
RESTORED_PROJECT = {
    'shortDescription': 'Milestones, implementation tasks and acceptance evidence.',
    'readme': ('Development milestones, implementation tasks and acceptance evidence for CTR2WUP.\n\n'
        '[Project graph](https://jdiller-86.github.io/CTR-2-WUP/progress/) · '
        '[Development plan](' + BASE + '/blob/main/plans.md).'),
}


def historical(client, name):
    if name not in HISTORY_BLOBS:
        raise project.TrackingError('Historical requirements are outside the reviewed scope.')
    result = client.api(f'repos/{project.REPOSITORY}/contents/docs/{name}?ref={HISTORY}')
    if (not isinstance(result, dict) or result.get('encoding') != 'base64'
            or result.get('type') != 'file' or result.get('path') != 'docs/' + name
            or result.get('sha') != HISTORY_BLOBS[name]):
        raise project.TrackingError('Historical requirements are unavailable.')
    content = base64.b64decode(''.join(result['content'].split()), validate=True)
    if len(content) > 1024*1024:
        raise project.TrackingError('Historical requirements exceed the review budget.')
    blob = b'blob ' + str(len(content)).encode('ascii') + b'\0' + content
    if hashlib.sha1(blob).hexdigest() != HISTORY_BLOBS[name]:
        raise project.TrackingError('Historical requirements content identity differs.')
    return json.loads(content)


def issues(client):
    rows = []
    for page in range(1,11):
        batch = client.api(f'repos/{project.REPOSITORY}/issues?state=all&per_page=100&page={page}')
        if not isinstance(batch,list):
            raise project.TrackingError('Repository issues could not be verified.')
        rows.extend(x for x in batch if 'pull_request' not in x)
        if len(batch)<100:
            return {x['number']:x for x in rows}
    raise project.TrackingError('Repository issue pagination exceeds the review budget.')


def labels(issue):
    return [x['name'] for x in issue.get('labels',[])]


def managed(issue):
    return issue['number'] in range(1,8) or issue['number'] in range(12,47) or issue['number']==80 or ('feature' in labels(issue) and issue['number']!=121)


def gate_body(gate, tasks):
    n=int(gate['id'][1:])
    body=(f'<!-- ctr2wup:milestone:m{n} -->\n## Scope\n\n'+gate['summary']+
          '\n\n## Implementation history\n\nHistorical checkpoints describe the scope '
          'and limitations at their recorded revisions.\n\n')
    body+=''.join('- '+s+'\n' for s in gate['verified_foundations'])
    body+='\n## Full milestone acceptance\n\n'+''.join('- [ ] '+s+'\n' for s in gate['acceptance_remaining'])
    body+='\n## Work breakdown\n\n'
    for index,task in enumerate(gate['work_items']):
        number=12+(n-1)*5+index
        done=tasks[number]['state']=='closed' and tasks[number].get('state_reason')!='not_planned'
        body+=f'- [{"x" if done else " "}] #{number}: {task["title"]}\n'
    body+=f'\n[Project]({project.PROJECT_URL}) · [Technical architecture]({BASE}/blob/main/docs/architecture.md)\n'
    return body


def task_body(task, parent, current):
    body=f'<!-- ctr2wup:work-item:{task["id"]} -->\n## Scope\n\n'+task['summary']+'\n\n## Acceptance criteria\n\n'
    body+=''.join('- [ ] '+s+'\n' for s in task['acceptance_criteria'])
    body+=f'\n## Implementation and evidence\n\n{current.strip()}\n\nMilestone overview: #{parent}.\n'
    return body


def restoration_plan(repo, live, requirements, features):
    if not set(range(1,8))<=repo.keys() or any(repo[n]['state']!='open' for n in range(1,8)):
        raise project.TrackingError('Seven open milestone gates are required; preserve reviewed acceptance.')
    if any(repo[n]['state']!='closed' or repo[n].get('state_reason')!='completed' for n in (12,14,27)):
        raise project.TrackingError('Completed baseline work must remain closed.')
    gates=requirements.get('milestones',[])
    if len(gates)!=7 or any(len(g['work_items'])!=5 for g in gates):
        raise project.TrackingError('Historical work breakdown differs from the reviewed scope.')
    tasks={t['id']:t for g in gates for t in g['work_items']}
    task_numbers={t['id']:12+i*5+j for i,g in enumerate(gates) for j,t in enumerate(g['work_items'])}
    selected=project.fields(live)
    options={o['name'].casefold():o['id'] for o in selected['Status']['options']}
    type_field=selected.get('Work type')
    type_options={o['name'].casefold():o['id'] for o in type_field.get('options',[])} if type_field else {}
    members={}
    for row in live['items']:
        issue=project.public_issue(row)
        if issue:
            if issue['number'] in members:
                raise project.TrackingError('Duplicate Project issue membership requires review.')
            members[issue['number']]=row
    wanted={n for n,d in repo.items() if managed(d)}
    if len(wanted)!=82 or not wanted<=members.keys():
        raise project.TrackingError('The original 82 issue memberships must be present, including archived history.')
    if any(project.status_value(members[n], selected)=='Accepted' for n in range(1,8)):
        raise project.TrackingError('Full milestone acceptance requires a separate evidence review.')
    if any(project.status_value(members[n], selected)!='Done' for n in (12,14,27)):
        raise project.TrackingError('Reviewed baseline Project statuses must remain Done.')
    operations=[]
    for n in sorted(wanted):
        issue,row=repo[n],members[n]
        body=issue.get('body') or ''
        restore=body.startswith(PREFIX)
        patch={}
        if n in REOPEN and issue['state']=='closed' and issue.get('state_reason')=='not_planned' and not restore:
            raise project.TrackingError('Consolidated issue state changed; preserve the new review.')
        if restore:
            if n not in REOPEN or issue['state']!='closed' or issue.get('state_reason')!='not_planned' or not body.startswith(BANNER):
                raise project.TrackingError('Consolidated issue state changed; preserve the new review.')
            patch={'state':'open','body':body[len(BANNER):]}
        if n in range(1,8) and body.startswith(f'Milestone M{n}: '):
            patch['body']=gate_body(gates[n-1],repo)
        if n in TASK_IDS and '<!-- ctr2wup:work-item:' not in body:
            patch['body']=task_body(tasks[TASK_IDS[n]],1+(n-12)//5,body)
        if n==80 and '<!-- ctr2wup:feature:pica-gx2 -->' not in body:
            feature=next(f for f in features['features'] if f['id']=='pica-gx2')
            patch['body']='<!-- ctr2wup:feature:pica-gx2 -->\n## Scope\n\n'+''.join('- '+s+'\n' for s in feature['scope'])+'\n## Acceptance criteria\n\n'+''.join('- [ ] '+s+'\n' for s in feature['acceptance'])+'\n## Implementation\n\n'+body+'\nParent task: #28.\n'
            patch['labels']=sorted(set(labels(issue)+['feature','work-item']))
        if patch:
            operations.append({'kind':'issue','number':n,'payload':patch})
        if row['isArchived']:
            operations.append({'kind':'unarchive','item':row['id']})
        parent=(row['content'].get('parent') or {}).get('number')
        expected=PARENTS.get(n)
        if expected and parent!=expected:
            if parent:
                operations.append({'kind':'remove_parent','parent':repo[parent]['node_id'],'child':issue['node_id']})
            operations.append({'kind':'add_parent','parent':repo[expected]['node_id'],'child':issue['node_id']})
        if restore:
            status=STATUSES[n]
            if status.casefold() not in options:
                raise project.TrackingError('Expected Project status option is unavailable.')
            operations.append({'kind':'field','item':row['id'],'field':selected['Status']['id'],'value':{'singleSelectOptionId':options[status.casefold()]}})
        if n==80 and type_field and 'feature' in type_options and (row.get('workType') or {}).get('name','').casefold()!='feature':
            operations.append({'kind':'field','item':row['id'],'field':type_field['id'],'value':{'singleSelectOptionId':type_options['feature']}})
    dependencies={12+i*5+j:[task_numbers[d] for d in t.get('depends_on',[])] for i,g in enumerate(gates) for j,t in enumerate(g['work_items'])}
    for n,blockers in dependencies.items():
        existing={d['number'] for d in members[n]['content'].get('blockedBy',{}).get('nodes',[]) if d.get('repository',{}).get('nameWithOwner')==project.REPOSITORY}
        for blocker in blockers:
            if blocker not in existing:
                operations.append({'kind':'blocker','issue':repo[n]['node_id'],'blocker':repo[blocker]['node_id']})
    metadata={name:RESTORED_PROJECT[name] for name,old in COMPACTED_PROJECT.items() if live.get(name)==old}
    if metadata:
        operations.append({'kind':'project','payload':{'projectId':live['id'],**metadata}})
    return operations,wanted


def verify_field_readback(operations, before, fresh):
    """A removed cleanup banner must not hide an ignored status mutation."""
    selected=project.fields(fresh)
    properties={'Status':'status','Work type':'workType','Waiting resource':'waiting'}
    fields_by_id={field['id']:(properties[name],field) for name,field in selected.items()}
    original={row['id']:row for row in before['items']}
    current={row['id']:row for row in fresh['items']}
    for op in operations:
        if op['kind']=='project':
            if (fresh.get('id')!=op['payload']['projectId']
                    or any(fresh.get(name)!=value for name,value in op['payload'].items() if name!='projectId')):
                raise project.TrackingError('Restoration Project text readback differs; review the current Project.')
            continue
        if op['kind']!='field':
            continue
        row=current.get(op['item'])
        previous=original.get(op['item'])
        definition=fields_by_id.get(op['field'])
        if (not row or not previous or not definition
                or row.get('content',{}).get('id')!=previous.get('content',{}).get('id')):
            raise project.TrackingError('Restoration field readback identity differs; review the current Project.')
        name,field=definition
        actual,expected=row.get(name),op['value']
        if expected is None:
            matches=actual is None
        elif 'singleSelectOptionId' in expected:
            option=expected['singleSelectOptionId']
            options={choice['id']:choice['name'] for choice in field.get('options',[])}
            matches=(isinstance(actual,dict) and option in options
                     and actual.get('__typename')=='ProjectV2ItemFieldSingleSelectValue'
                     and actual.get('optionId')==option and actual.get('name')==options[option])
        elif 'text' in expected:
            matches=(isinstance(actual,dict) and actual.get('__typename')=='ProjectV2ItemFieldTextValue'
                     and actual.get('text')==expected['text'])
        else:
            matches=False
        if not matches:
            raise project.TrackingError('Restoration field readback differs; review the current Project before retrying.')


def restore(client, apply=False):
    if apply:
        project.trusted_main(client)
    requirements=historical(client,'milestone-status.json')
    features=historical(client,'features.json')
    repo,live=issues(client),client.project()
    operations,wanted=restoration_plan(repo,live,requirements,features)
    report={'ok':True,'applied':apply,'operation_count':len(operations),'completed_operations':0,
        'reopen_issues':[o['number'] for o in operations if o['kind']=='issue' and o['payload'].get('state')=='open'],
        'unarchive_issues':sum(o['kind']=='unarchive' for o in operations),'retained_completed':[12,14,27],
        'milestones_accepted':0,'expected_issue_count':len(wanted)}
    for op in operations if apply else []:
        kind=op['kind']
        if kind=='issue':
            client.api(f'repos/{project.REPOSITORY}/issues/{op["number"]}','PATCH',op['payload'])
        elif kind=='unarchive':
            client.mutate('unarchiveProjectV2Item','UnarchiveProjectV2ItemInput',{'projectId':live['id'],'itemId':op['item']})
        elif kind in ('add_parent','remove_parent'):
            name='addSubIssue' if kind=='add_parent' else 'removeSubIssue'
            client.mutate(name,name[0].upper()+name[1:]+'Input',{'issueId':op['parent'],'subIssueId':op['child']})
        elif kind=='blocker':
            client.mutate('addBlockedBy','AddBlockedByInput',{'issueId':op['issue'],'blockingIssueId':op['blocker']})
        elif kind=='project':
            client.mutate('updateProjectV2','UpdateProjectV2Input',op['payload'])
        else:
            payload={'projectId':live['id'],'itemId':op['item'],'fieldId':op['field']}
            if op['value'] is None:
                client.mutate('clearProjectV2ItemFieldValue','ClearProjectV2ItemFieldValueInput',payload)
            else:
                client.mutate('updateProjectV2ItemFieldValue','UpdateProjectV2ItemFieldValueInput',{**payload,'value':op['value']})
        report['completed_operations']+=1
    if apply:
        fresh_repo,fresh_live=issues(client),client.project()
        verify_field_readback(operations,live,fresh_live)
        remaining,_=restoration_plan(fresh_repo,fresh_live,requirements,features)
        if remaining:
            raise project.TrackingError('Restoration readback differs; preview the current state before retrying.')
        report['readback_verified']=True
    return report


if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply',action='store_true')
    args=parser.parse_args()
    try:
        report=restore(project.GitHub(),args.apply)
    except (project.TrackingError,ValueError,KeyError,TypeError,OSError) as error:
        report={'ok':False,'applied':args.apply,'error':str(error) if isinstance(error,project.TrackingError) else 'Tracking restoration could not complete.'}
    output=project.ROOT/'artifacts/project-restoration.json'
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))
    raise SystemExit(0 if report['ok'] else 2)

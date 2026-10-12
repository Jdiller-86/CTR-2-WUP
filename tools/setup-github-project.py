#!/usr/bin/env python3
"""Prepare the private milestone and actionable-work board; dry-run by default.

Uses normal gh authentication. --apply changes only Projects: it never changes
repository visibility/settings, issue bodies, paid features or token storage.
Existing notes, other items, option IDs and nonempty item values are preserved.
Access denials and ambiguous existing data fail before writes. A partial API
failure is reported explicitly; run a fresh dry-run before retrying.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
STATUSES = ('Planned', 'In progress', 'Awaiting evidence', 'Blocked',
            'Ready for review', 'Done', 'Closed', 'Accepted')
COLORS = dict(zip(STATUSES, ('GRAY', 'BLUE', 'YELLOW', 'RED',
                             'PURPLE', 'GREEN', 'GRAY', 'GREEN')))
WORK_ITEM_STATUSES = {'planned': 'Planned', 'in_progress': 'In progress',
                      'awaiting_evidence': 'Awaiting evidence', 'blocked': 'Blocked'}
MANAGED_MARKER = re.compile(r'<!--\s*ctr2wup:(milestone|work-item):([^\s<>]+)\s*-->')
RESOURCE_NAMES = {'needs-console': 'Wii U console results',
                  'needs-input': 'Owned decrypted executable and assets',
                  'needs-windows': 'Native Windows conversion validation'}
VIEW_NAME = 'Milestones'

PROJECTS_QUERY = '''query($login:String!,$after:String) {
  repositoryOwner(login:$login) {
    id login
    ... on User { projectsV2(first:100,after:$after) {
      nodes { id title number url public closed viewerCanUpdate }
      pageInfo { hasNextPage endCursor }
    } }
    ... on Organization { projectsV2(first:100,after:$after) {
      nodes { id title number url public closed viewerCanUpdate }
      pageInfo { hasNextPage endCursor }
    } }
  }
}'''
FIELD_SELECTION = '''nodes {
  __typename
  ... on ProjectV2Field { id name dataType }
  ... on ProjectV2SingleSelectField { id name dataType options { id name color description } }
  ... on ProjectV2IterationField { id name dataType }
  ... on ProjectV2MultiSelectField { id name dataType }
} pageInfo { hasNextPage endCursor }'''
ITEM_SELECTION = '''nodes {
  id isArchived
  content {
    __typename
    ... on Issue { id number url }
    ... on PullRequest { id }
    ... on DraftIssue { id }
  }
  status:fieldValueByName(name:"Status") {
    __typename ... on ProjectV2ItemFieldSingleSelectValue { optionId }
  }
  waiting:fieldValueByName(name:"Waiting resource") {
    __typename ... on ProjectV2ItemFieldTextValue { text }
  }
  milestone:fieldValueByName(name:"Milestone") {
    __typename ... on ProjectV2ItemFieldTextValue { text }
  }
} pageInfo { hasNextPage endCursor }'''
VIEW_SELECTION = '''nodes {
  id name layout
  configuration { visibleFields(first:100) {
    nodes {
      ... on ProjectV2Field { id }
      ... on ProjectV2SingleSelectField { id }
      ... on ProjectV2IterationField { id }
      ... on ProjectV2MultiSelectField { id }
    }
    pageInfo { hasNextPage }
  } }
} pageInfo { hasNextPage endCursor }'''


class SetupError(RuntimeError):
    pass


class GitHub:
    def __init__(self):
        self.write_attempted = False

    def _call(self, arguments, payload=None):
        try:
            result = subprocess.run(['gh', 'api', *arguments],
                                    input=json.dumps(payload) if payload is not None else None,
                                    capture_output=True, text=True, encoding='utf-8', timeout=60)
        except (OSError, subprocess.TimeoutExpired) as error:
            raise SetupError('GitHub API could not complete: ' + type(error).__name__) from error
        if result.returncode:
            raise SetupError('GitHub API access/request failed: ' + result.stderr.strip())
        try:
            response = json.loads(result.stdout)
        except ValueError as error:
            raise SetupError('GitHub returned an invalid JSON response') from error
        return response

    def rest(self, path):
        return self._call([path])

    def graphql(self, query, variables):
        response = self._call(['graphql', '--input', '-'], {'query': query, 'variables': variables})
        if response.get('errors'):
            errors = '; '.join(error.get('message', 'Unknown GraphQL error') for error in response['errors'])
            raise SetupError('GitHub Projects access/request failed: ' + errors)
        if not isinstance(response.get('data'), dict):
            raise SetupError('GitHub Projects returned no data')
        return response['data']

    def issues(self, repo):
        items = []
        for page in range(1, 11):
            data = self.rest(f'repos/{repo}/issues?state=all&per_page=100&page={page}')
            if not isinstance(data, list):
                raise SetupError('Unexpected issue list response')
            items.extend(data)
            if len(data) < 100:
                return items
        raise SetupError('Issue list exceeds the 1,000-entry review budget')

    def projects(self, owner):
        items, after = [], None
        for _ in range(10):
            data = self.graphql(PROJECTS_QUERY, {'login': owner, 'after': after})
            node = data.get('repositoryOwner')
            if not node or not isinstance(node.get('projectsV2'), dict):
                raise SetupError('Project owner/read access could not be verified')
            connection = node['projectsV2']
            items.extend(connection['nodes'])
            if not connection['pageInfo']['hasNextPage']:
                return items
            after = connection['pageInfo']['endCursor']
        raise SetupError('Project list exceeds the 1,000-entry review budget')

    def project(self, owner, number):
        # Explicit user selection avoids touching unrelated inaccessible boards.
        query = '''query($login:String!,$number:Int!) {
          repositoryOwner(login:$login) {
            ... on User { projectV2(number:$number) { id } }
            ... on Organization { projectV2(number:$number) { id } }
          }
        }'''
        node = self.graphql(query, {'login': owner, 'number': number}).get('repositoryOwner')
        return node.get('projectV2') if node else None

    def _connection(self, project_id, name, selection):
        nodes, after = [], None
        query = ('query($id:ID!,$after:String) { node(id:$id) { ... on ProjectV2 { '
                 + name + '(first:100,after:$after) { ' + selection + ' } } } }')
        for _ in range(10):
            project = self.graphql(query, {'id': project_id, 'after': after}).get('node')
            if not project or name not in project:
                raise SetupError('Existing project/read access could not be verified')
            connection = project[name]
            nodes.extend(connection['nodes'])
            if not connection['pageInfo']['hasNextPage']:
                return nodes
            after = connection['pageInfo']['endCursor']
        raise SetupError(f'Project {name} exceed the 1,000-entry review budget')

    def snapshot(self, project_id):
        query = '''query($id:ID!) { node(id:$id) { ... on ProjectV2 {
          id title number url public closed viewerCanUpdate
          repositories(first:100) { nodes { id } pageInfo { hasNextPage } }
        } } }'''
        project = self.graphql(query, {'id': project_id}).get('node')
        if not project:
            raise SetupError('Existing project is inaccessible')
        if project['repositories']['pageInfo']['hasNextPage']:
            raise SetupError('Linked repositories exceed the 100-entry review budget')
        project['fields'] = self._connection(project_id, 'fields', FIELD_SELECTION)
        project['items'] = self._connection(project_id, 'items', ITEM_SELECTION)
        project['views'] = self._connection(project_id, 'views', VIEW_SELECTION)
        return project

    def mutate(self, name, input_type, payload, selection):
        query = f'mutation($input:{input_type}!) {{ {name}(input:$input) {{ {selection} }} }}'
        self.write_attempted = True
        result = self.graphql(query, {'input': payload}).get(name)
        if not result:
            raise SetupError('GitHub did not confirm mutation ' + name)
        return result


def configuration(settings, state):
    repo = settings.get('repository', '')
    if settings.get('schema_version') != 1 or not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repo):
        raise SetupError('Review repository settings schema/name before setup')
    requested = settings.get('project_requested', {})
    if (requested.get('public') is not False or requested.get('title') != 'CTR2WUP Development'
            or requested.get('status_columns') != list(STATUSES)
            or requested.get('extra_fields') != ['Milestone', 'Waiting resource']):
        raise SetupError('Review the private board configuration before setup')
    existing_url = requested.get('existing_url')
    if existing_url is not None:
        match = re.fullmatch(r'https://github\.com/users/([A-Za-z0-9-]+)/projects/([1-9][0-9]{0,8})/?', existing_url)
        if not match or match[1].lower() != repo.split('/')[0].lower():
            raise SetupError('Existing Project URL must identify this repository owner’s GitHub Project')
    milestones = state.get('milestones', [])
    if (state.get('schema_version') != 1
            or [m.get('id') for m in milestones] != [f'm{i}' for i in range(1, 8)]
            or len({m.get('title') for m in milestones}) != 7):
        raise SetupError('Require seven distinct versioned milestone trackers')
    accepted = state.get('full_milestones_accepted')
    if type(accepted) is not int or not 0 <= accepted <= 7:
        raise SetupError('Invalid full milestone acceptance count')
    work_items = []
    for milestone in milestones:
        if set(milestone.get('waiting_labels', [])) - RESOURCE_NAMES.keys():
            raise SetupError('Unknown waiting resource label: ' + milestone['id'])
        if milestone.get('project_status') not in (*STATUSES, None):
            raise SetupError('Unknown project status: ' + milestone['id'])
        if milestone.get('state') not in (*WORK_ITEM_STATUSES, None):
            raise SetupError('Unknown milestone state: ' + milestone['id'])
        if milestone.get('project_status') == 'Accepted' and milestone.get('accepted') is not True:
            raise SetupError('Accepted status requires explicit accepted evidence: ' + milestone['id'])
        tasks = milestone.get('work_items', [])
        if not isinstance(tasks, list):
            raise SetupError('Work items must be a list: ' + milestone['id'])
        for task in tasks:
            identifier = task.get('id') if isinstance(task, dict) else None
            if (not isinstance(identifier, str) or len(identifier) > 96
                    or not re.fullmatch(re.escape(milestone['id']) + r'-[a-z0-9]+(?:-[a-z0-9]+)*', identifier)):
                raise SetupError('Invalid actionable work-item ID: ' + milestone['id'])
            if not isinstance(task.get('title'), str) or not task['title'].strip():
                raise SetupError('Actionable work item requires a title: ' + identifier)
            if task.get('state') not in WORK_ITEM_STATUSES:
                raise SetupError('Unknown actionable work-item state: ' + identifier)
            if task.get('project_status') == 'Accepted' or task.get('accepted') is True:
                raise SetupError('Accepted is reserved for full milestone evidence: ' + identifier)
            labels = task.get('waiting_labels', [])
            if not isinstance(labels, list) or any(label not in RESOURCE_NAMES for label in labels):
                raise SetupError('Unknown actionable waiting resource: ' + identifier)
            dependencies = task.get('depends_on', [])
            if (not isinstance(dependencies, list)
                    or any(not isinstance(dep, str) for dep in dependencies)
                    or len(set(dependencies)) != len(dependencies)):
                raise SetupError('Invalid actionable dependencies: ' + identifier)
            work_items.append(task)
    identifiers = [task['id'] for task in work_items]
    if len(set(identifiers)) != len(identifiers) or set(identifiers) & {m['id'] for m in milestones}:
        raise SetupError('Duplicate managed work-item IDs; review before writes')
    known_ids = set(identifiers) | {m['id'] for m in milestones}
    graph = {task['id']: task.get('depends_on', []) for task in work_items}
    for task in work_items:
        if task['id'] in task.get('depends_on', []) or set(task.get('depends_on', [])) - known_ids:
            raise SetupError('Unknown or self-referencing actionable dependency: ' + task['id'])
    visited, visiting = set(), set()
    def visit(identifier):
        if identifier in visiting:
            raise SetupError('Cyclic actionable dependency: ' + identifier)
        if identifier in visited or identifier not in graph:
            return
        visiting.add(identifier)
        for dependency in graph[identifier]:
            visit(dependency)
        visiting.remove(identifier)
        visited.add(identifier)
    for identifier in graph:
        visit(identifier)
    return repo


def initial_status(milestone):
    explicit = milestone.get('project_status')
    if explicit:
        return explicit
    if milestone.get('state') is not None:
        return WORK_ITEM_STATUSES[milestone['state']]
    if milestone['id'] == 'm7':
        return 'Planned'  # The current source requirements defer the GUI.
    if milestone.get('waiting_labels'):
        return 'Awaiting evidence'
    return 'In progress' if milestone.get('verified_foundations') else 'Planned'


def base_report(settings, state):
    repo = configuration(settings, state)
    return {'schema_version': 1, 'ok': False, 'applied': False,
            'repository': repo, 'version': state['version'],
            'requested_project': {'title': settings['project_requested']['title'], 'public': False,
                                  'existing_url': settings['project_requested'].get('existing_url'),
                                  'view': VIEW_NAME, 'layout': 'BOARD_LAYOUT',
                                  'status_options': list(STATUSES),
                                  'fields': ['Milestone', 'Waiting resource']},
            'policy': {'preserve_notes_other_items_and_existing_values': True,
                       'preserve_existing_option_ids_and_extra_options': True,
                       'initialize_empty_managed_values_only': True,
                       'change_repository_settings_visibility_or_paid_features': False},
            'trackers': [{'id': m['id'], 'title': m['title'], 'kind': 'milestone',
                          'milestone_id': m['id'], 'milestone_title': m['title'],
                          'initial_status': initial_status(m),
                          'waiting_resource': '; '.join(RESOURCE_NAMES[label] for label in m['waiting_labels'])}
                         for m in state['milestones']],
            'work_items': [{'id': task['id'], 'title': task['title'], 'kind': 'work-item',
                            'milestone_id': m['id'], 'milestone_title': m['title'],
                            'initial_status': WORK_ITEM_STATUSES[task['state']],
                            'waiting_resource': '; '.join(RESOURCE_NAMES[label] for label in task.get('waiting_labels', []))}
                           for m in state['milestones'] for task in m.get('work_items', [])],
            'operations': [], 'completed_operations': [],
            'project_url': settings['project_requested'].get('existing_url'),
            'live_project_verified': False}


def unique_named(nodes, kind):
    result = {}
    for node in nodes:
        name = node.get('name')
        if not name or name in result:
            raise SetupError('Duplicate or missing ' + kind + ' name; review before writes')
        result[name] = node
    return result


def validate_project(project):
    if project.get('public') is not False:
        raise SetupError('Matching project is public; setup will not change its visibility')
    if project.get('closed'):
        raise SetupError('Matching project is closed; setup will not reopen it')
    if project.get('viewerCanUpdate') is not True:
        raise SetupError('Existing project write access was denied; no changes made')
    fields = unique_named(project['fields'], 'field')
    for name, allowed in {'Status': {'SINGLE_SELECT'}, 'Milestone': {'MILESTONE', 'TEXT'},
                          'Waiting resource': {'TEXT'}, 'Title': {'TITLE'}}.items():
        if name in fields and fields[name].get('dataType') not in allowed:
            raise SetupError('Existing field has incompatible type: ' + name)
    if 'Status' in fields:
        options = fields['Status'].get('options', [])
        unique_named(options, 'Status option')
        identifiers = [option.get('id') for option in options]
        if None in identifiers or len(set(identifiers)) != len(identifiers):
            raise SetupError('Duplicate or missing Status option IDs')
    items = {}
    for item in project['items']:
        content = item.get('content')
        if not content:
            continue  # Preserve inaccessible/deleted unrelated items untouched.
        identifier = content.get('id')
        if not identifier or identifier in items:
            raise SetupError('Duplicate project content items; review before writes')
        items[identifier] = item
    views = unique_named(project['views'], 'view')
    for view in views.values():
        if view['configuration']['visibleFields']['pageInfo']['hasNextPage']:
            raise SetupError('View fields exceed the 100-entry review budget')
    if VIEW_NAME in views and views[VIEW_NAME]['layout'] != 'BOARD_LAYOUT':
        raise SetupError('Existing Milestones view is not a board; preserve and review it')
    return fields, items, views


def preflight(client, settings, state, report):
    repo = configuration(settings, state)
    repository = client.rest('repos/' + repo)
    if repository.get('full_name', '').lower() != repo.lower() or not repository.get('node_id'):
        raise SetupError('Repository identity/read access could not be verified')
    owner = repository['owner']
    if not owner.get('node_id') or not owner.get('login'):
        raise SetupError('Repository owner identity is missing')
    issues, trackers, work_items, managed_node_ids = client.issues(repo), [], [], set()
    milestones = {m['id']: m for m in state['milestones']}
    for planned in [*report['trackers'], *report['work_items']]:
        milestone = milestones[planned['milestone_id']]
        marker = '<!-- ctr2wup:' + planned['kind'] + ':' + planned['id'] + ' -->'
        matched = [i for i in issues if 'pull_request' not in i and marker in (i.get('body') or '')]
        if len(matched) != 1 or not matched[0].get('node_id'):
            raise SetupError('Require one existing managed ' + planned['kind'] + ' issue for '
                             + planned['id'] + '; run the issue sync before board setup')
        issue = matched[0]
        markers = MANAGED_MARKER.findall(issue.get('body') or '')
        if markers != [(planned['kind'], planned['id'])]:
            raise SetupError('Duplicate or ambiguous managed issue marker: ' + planned['id'])
        if issue['node_id'] in managed_node_ids:
            raise SetupError('Multiple managed work items resolve to one issue: ' + planned['id'])
        managed_node_ids.add(issue['node_id'])
        if not issue.get('milestone') or issue['milestone'].get('title') != milestone['title']:
            raise SetupError('Managed issue milestone association needs review: ' + planned['id'])
        planned.update(number=issue['number'], url=issue['html_url'])
        (trackers if planned['kind'] == 'milestone' else work_items).append(
            {'issue': issue, 'milestone': milestone, 'planned': planned})
    existing_url = settings['project_requested'].get('existing_url')
    if existing_url:
        project_number = int(existing_url.rstrip('/').rsplit('/', 1)[1])
        selected = client.project(owner['login'], project_number)
        if not selected or not selected.get('id'):
            raise SetupError('Requested existing Project is inaccessible; no new Project will be created')
        project = client.snapshot(selected['id'])
        if project.get('number') != project_number or project.get('url', '').rstrip('/').lower() != existing_url.rstrip('/').lower():
            raise SetupError('Existing Project identity changed; no changes made')
    else:
        projects = [p for p in client.projects(owner['login']) if p['title'] == settings['project_requested']['title']]
        if len(projects) > 1:
            raise SetupError('Duplicate project titles; select/resolve them before writes')
        project = client.snapshot(projects[0]['id']) if projects else None
    if project:
        _, items, _ = validate_project(project)
        for tracker in [*trackers, *work_items]:
            item = items.get(tracker['issue']['node_id'])
            if item and item.get('isArchived'):
                raise SetupError('Managed item is archived; preserve and review ' + tracker['planned']['id'])
        report['project_url'] = project['url']
        report['live_project_verified'] = True
    return {'repository': repository, 'owner': owner, 'project': project,
            'trackers': trackers, 'work_items': work_items}


def managed_items(context):
    return [*context['trackers'], *context['work_items']]


def options_for(field):
    options = [dict(option) for option in field.get('options', [])] if field else []
    names = {option['name'] for option in options}
    return options + [{'name': name, 'color': COLORS[name], 'description': ''}
                      for name in STATUSES if name not in names]


def empty_value(item, key):
    value = item.get(key) if item else None
    if not value:
        return True
    return not value.get('optionId') if key == 'status' else not value.get('text')


def plan(context, report):
    project = context['project']
    fields, items, views = validate_project(project) if project else ({}, {}, {})
    operations = report['operations']
    if not project:
        operations.append({'kind': 'create-private-project', 'title': report['requested_project']['title']})
    if not project or context['repository']['node_id'] not in {r['id'] for r in project['repositories']['nodes']}:
        operations.append({'kind': 'link-repository', 'repository': report['repository']})
    status = fields.get('Status')
    missing = [name for name in STATUSES if not status or name not in {o['name'] for o in status['options']}]
    if missing:
        operations.append({'kind': 'create-status-field' if not status else 'append-status-options', 'options': missing})
    for name in ('Milestone', 'Waiting resource'):
        if name not in fields:
            operations.append({'kind': 'ensure-field', 'name': name,
                               'data_type': 'MILESTONE_OR_TEXT' if name == 'Milestone' else 'TEXT'})
    view = views.get(VIEW_NAME)
    if not view:
        operations.append({'kind': 'create-board-view', 'name': VIEW_NAME})
    else:
        visible = {node['id'] for node in view['configuration']['visibleFields']['nodes']}
        if any(name not in fields or fields[name]['id'] not in visible for name in ('Title', 'Status', 'Milestone', 'Waiting resource')):
            operations.append({'kind': 'append-visible-board-fields', 'name': VIEW_NAME})
    for tracker in managed_items(context):
        issue, planned = tracker['issue'], tracker['planned']
        item = items.get(issue['node_id'])
        if not item:
            operations.append({'kind': 'add-existing-issue', 'id': planned['id'], 'number': issue['number']})
        initialize = []
        for key, name in (('status', 'Status'), ('waiting', 'Waiting resource')):
            if empty_value(item, key) and (key != 'waiting' or planned['waiting_resource']):
                initialize.append(name)
        if (not project or 'Milestone' not in fields or fields['Milestone']['dataType'] == 'TEXT') and empty_value(item, 'milestone'):
            initialize.append('Milestone')
        if initialize:
            operations.append({'kind': 'initialize-empty-fields', 'id': planned['id'], 'fields': initialize})
        planned['preserve_existing_values'] = bool(item)
    report['ok'] = True
    return report


def apply(client, context, report):
    completed = report['completed_operations']
    owner, repository, project = context['owner'], context['repository'], context['project']
    if not project:
        # Recheck names immediately before creating to avoid a stale dry-run.
        if any(p['title'] == report['requested_project']['title'] for p in client.projects(owner['login'])):
            raise SetupError('Project appeared after preflight; run a fresh dry-run')
        created = client.mutate('createProjectV2', 'CreateProjectV2Input',
                                {'ownerId': owner['node_id'], 'title': report['requested_project']['title'],
                                 'repositoryId': repository['node_id']},
                                'projectV2 { id url public }')['projectV2']
        completed.append({'kind': 'create-private-project', 'url': created['url']})
        report['project_url'] = created['url']
        if created.get('public') is not False:
            raise SetupError('Created project is unexpectedly public; no visibility change attempted')
        project = client.snapshot(created['id'])
        validate_project(project)
        fields = unique_named(project['fields'], 'field')
        if 'Status' in fields:
            # Only a brand-new empty project may replace its automatic defaults.
            if project['items']:
                raise SetupError('New project gained items concurrently; preserve and run a fresh dry-run')
            client.mutate('updateProjectV2Field', 'UpdateProjectV2FieldInput',
                          {'fieldId': fields['Status']['id'], 'singleSelectOptions': options_for(None)},
                          'projectV2Field { ... on ProjectV2SingleSelectField { id } }')
            completed.append({'kind': 'initialize-new-status-options'})
    project_id = project['id']
    if repository['node_id'] not in {r['id'] for r in project['repositories']['nodes']}:
        client.mutate('linkProjectV2ToRepository', 'LinkProjectV2ToRepositoryInput',
                      {'projectId': project_id, 'repositoryId': repository['node_id']}, 'repository { id }')
        completed.append({'kind': 'link-repository'})
    project = client.snapshot(project_id)
    fields, _, _ = validate_project(project)
    status = fields.get('Status')
    if not status:
        client.mutate('createProjectV2Field', 'CreateProjectV2FieldInput',
                      {'projectId': project_id, 'dataType': 'SINGLE_SELECT', 'name': 'Status',
                       'singleSelectOptions': options_for(None)},
                      'projectV2Field { ... on ProjectV2SingleSelectField { id } }')
        completed.append({'kind': 'create-status-field'})
    elif any(name not in {o['name'] for o in status['options']} for name in STATUSES):
        client.mutate('updateProjectV2Field', 'UpdateProjectV2FieldInput',
                      {'fieldId': status['id'], 'singleSelectOptions': options_for(status)},
                      'projectV2Field { ... on ProjectV2SingleSelectField { id } }')
        completed.append({'kind': 'append-status-options'})
    for name in ('Milestone', 'Waiting resource'):
        if name not in fields:
            client.mutate('createProjectV2Field', 'CreateProjectV2FieldInput',
                          {'projectId': project_id, 'dataType': 'TEXT', 'name': name},
                          'projectV2Field { ... on ProjectV2Field { id } }')
            completed.append({'kind': 'create-field', 'name': name})
    project = client.snapshot(project_id)
    fields, items, views = validate_project(project)
    visible = [fields[name]['id'] for name in ('Title', 'Status', 'Milestone', 'Waiting resource') if name in fields]
    view = views.get(VIEW_NAME)
    if not view:
        client.mutate('createProjectV2View', 'CreateProjectV2ViewInput',
                      {'projectId': project_id, 'name': VIEW_NAME, 'layout': 'BOARD_LAYOUT',
                       'configuration': {'visibleFieldIds': visible}}, 'projectV2View { id }')
        completed.append({'kind': 'create-board-view'})
    else:
        prior = [node['id'] for node in view['configuration']['visibleFields']['nodes']]
        merged = prior + [identifier for identifier in visible if identifier not in prior]
        if merged != prior:
            client.mutate('updateProjectV2View', 'UpdateProjectV2ViewInput',
                          {'viewId': view['id'], 'configuration': {'visibleFieldIds': merged}},
                          'projectV2View { id }')
            completed.append({'kind': 'append-visible-board-fields'})
    for tracker in managed_items(context):
        issue, planned = tracker['issue'], tracker['planned']
        if issue['node_id'] not in items:
            client.mutate('addProjectV2ItemById', 'AddProjectV2ItemByIdInput',
                          {'projectId': project_id, 'contentId': issue['node_id']}, 'item { id }')
            completed.append({'kind': 'add-existing-issue', 'id': planned['id']})
        # Refresh before each managed item; preserve values entered concurrently.
        project = client.snapshot(project_id)
        fields, items, _ = validate_project(project)
        item = items.get(issue['node_id'])
        if not item or item.get('isArchived'):
            raise SetupError('Managed project item unavailable/archived after setup: ' + planned['id'])
        status_option = next((o['id'] for o in fields['Status']['options'] if o['name'] == planned['initial_status']), None)
        if status_option is None:
            raise SetupError('Required Status option disappeared; preserve and review ' + planned['initial_status'])
        values = [('status', 'Status', {'singleSelectOptionId': status_option}),
                  ('waiting', 'Waiting resource', {'text': planned['waiting_resource']})]
        if fields['Milestone']['dataType'] == 'TEXT':
            values.append(('milestone', 'Milestone', {'text': planned['milestone_title']}))
        for key, name, value in values:
            if empty_value(item, key) and (key != 'waiting' or planned['waiting_resource']):
                client.mutate('updateProjectV2ItemFieldValue', 'UpdateProjectV2ItemFieldValueInput',
                              {'projectId': project_id, 'itemId': item['id'], 'fieldId': fields[name]['id'], 'value': value},
                              'projectV2Item { id }')
                completed.append({'kind': 'initialize-empty-field', 'id': planned['id'], 'field': name})
    final = client.snapshot(project_id)
    final_fields, final_items, final_views = validate_project(final)
    if (repository['node_id'] not in {r['id'] for r in final['repositories']['nodes']}
            or any(tracker['issue']['node_id'] not in final_items for tracker in managed_items(context))
            or any(name not in {o['name'] for o in final_fields['Status']['options']} for name in STATUSES)
            or VIEW_NAME not in final_views):
        raise SetupError('Final board verification failed; review completed operations')
    report.update(ok=True, applied=True, live_project_verified=True, project_url=final['url'])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true', help='Apply the preflighted board plan using normal gh authentication')
    parser.add_argument('--output', type=Path, help='Write the reviewable plan/result JSON')
    args = parser.parse_args(argv)
    report = {'schema_version': 1, 'ok': False, 'applied': False, 'completed_operations': []}
    client = None
    try:
        settings = json.loads((ROOT / '.github/repository-settings.json').read_text())
        state = json.loads((ROOT / 'docs/milestone-status.json').read_text())
        report = base_report(settings, state)
        client = GitHub()
        context = preflight(client, settings, state, report)
        plan(context, report)
        if args.apply:
            apply(client, context, report)
    except (SetupError, ValueError, KeyError, OSError) as error:
        report.update(ok=False, error=str(error), partial_changes=bool(report['completed_operations']),
                      changes_possible=bool(getattr(client, 'write_attempted', False)))
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0 if report['ok'] else 2


if __name__ == '__main__':
    sys.exit(main())

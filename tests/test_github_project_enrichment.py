"""Project metadata tests use original fixtures and never call GitHub."""
import copy
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('project_enrichment', ROOT / 'tools/flesh-github-project.py')
flesh = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flesh)
from tests.test_github_project import FakeGitHub, project, source


class EnrichmentGitHub(FakeGitHub):
    def __init__(self, state):
        super().__init__(state)
        self.extra_values = {}

    def graphql(self, query, variables):
        value = self.extra_values.get((variables['id'], variables['name']))
        return {'node': {'fieldValueByName': copy.deepcopy(value)}}

    def mutate(self, name, input_type, payload, selection):
        if name == 'updateProjectV2':
            self.writes.append((name, copy.deepcopy(payload)))
            self.remote_project['readme'] = payload['readme']
            return {'projectV2': {'id': self.remote_project['id']}}
        if name == 'createProjectV2Field' and payload['name'] in flesh.FIELDS:
            self.writes.append((name, copy.deepcopy(payload)))
            field = dict(id=payload['name'], name=payload['name'], dataType='SINGLE_SELECT',
                         options=[dict(row, id=payload['name'] + str(i))
                                  for i, row in enumerate(payload['singleSelectOptions'])])
            self.remote_project['fields'].append(field)
            return {'projectV2Field': {'id': field['id']}}
        if name == 'updateProjectV2View':
            self.writes.append((name, copy.deepcopy(payload)))
            return {'projectV2View': {'id': payload['viewId']}}
        if name == 'updateProjectV2ItemFieldValue' and payload['fieldId'] in flesh.FIELDS:
            self.writes.append((name, copy.deepcopy(payload)))
            self.extra_values[(payload['itemId'], payload['fieldId'])] = {'optionId': payload['value']['singleSelectOptionId']}
            return {'projectV2Item': {'id': payload['itemId']}}
        return super().mutate(name, input_type, payload, selection)


class ProjectEnrichmentTests(unittest.TestCase):
    def setUp(self):
        self.settings, self.state = source()
        self.api = EnrichmentGitHub(self.state)
        self.content = 'Original project requirements and acceptance policy.'
        self.report = flesh.setup.base_report(self.settings, self.state)
        self.context = flesh.setup.preflight(self.api, self.settings, self.state, self.report)

    def test_overview_preserves_notes_and_replaces_only_marked_section(self):
        first = flesh.overview('Owner’s private notes', 'first')
        second = flesh.overview(first + '\nKeep after section', 'new')
        self.assertEqual(second, 'Owner’s private notes\n\n' + flesh.BEGIN + '\nnew\n' + flesh.END + '\nKeep after section')
        self.assertEqual(flesh.overview(second, 'new'), second)

    def test_malformed_overview_never_changes_notes(self):
        for text in (flesh.BEGIN, flesh.END, flesh.END + flesh.BEGIN,
                     flesh.BEGIN * 2 + flesh.END):
            with self.assertRaises(flesh.setup.SetupError):
                flesh.overview(text, self.content)

    def test_dry_run_is_read_only_and_includes_fields_views_and_readme(self):
        previous = copy.deepcopy(self.api.remote_project)
        flesh.enrich(self.api, self.context, self.report, self.content)
        self.assertEqual(self.api.writes, [])
        self.assertEqual(self.api.remote_project, previous)
        self.assertEqual(len(self.report['enrichment_operations']), 7)

    def test_wrong_existing_field_type_fails_before_any_write(self):
        self.api.remote_project['fields'].append({'id': 'owner-priority', 'name': 'Priority', 'dataType': 'TEXT'})
        self.context['project'] = self.api.snapshot('project-1')
        with self.assertRaisesRegex(flesh.setup.SetupError, 'different type'):
            flesh.enrich(self.api, self.context, self.report, self.content, True)
        self.assertEqual(self.api.writes, [])

    def test_apply_keeps_manual_status_notes_and_existing_view(self):
        before = copy.deepcopy(self.api.remote_project)
        self.api.extra_values[('item-m1', 'Priority')] = {'optionId': 'owner-critical'}
        flesh.enrich(self.api, self.context, self.report, self.content, True)
        after = self.api.remote_project
        self.assertEqual(after['views'][0], before['views'][0])
        self.assertTrue(after['readme'].startswith(before['readme']))
        for previous, current in zip(before['items'], after['items']):
            self.assertEqual(previous, current)
        self.assertEqual(self.api.extra_values[('item-m1', 'Priority')]['optionId'], 'owner-critical')
        self.assertFalse(any(payload.get('itemId') == 'manual-item' for _, payload in self.api.writes))
        self.assertFalse(any(name == 'createProjectV2' for name, _ in self.api.writes))

    def test_existing_extra_options_are_preserved_including_ids(self):
        field = {'options': [{'id': 'manual', 'name': 'Owner priority', 'color': 'PINK', 'description': 'Owner text'}]}
        combined = flesh.additions(field, flesh.FIELDS['Priority'])
        self.assertEqual(combined[0], field['options'][0])
        self.assertEqual(len(combined), 5)

    def test_missing_project_cannot_create_a_replacement(self):
        self.context['project'] = None
        with self.assertRaisesRegex(flesh.setup.SetupError, 'no creation fallback'):
            flesh.enrich(self.api, self.context, self.report, self.content, True)
        self.assertEqual(self.api.writes, [])


if __name__ == '__main__':
    unittest.main()

#!/usr/bin/env python3
"""Apply the requested free repository features; never change visibility/protection."""
import argparse
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('docs_publisher', ROOT / 'tools/publish-docs-site.py')
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)
REPOSITORY = 'Jdiller-86/CTR-2-WUP'


def configure(client, settings, apply=False):
    if (settings.get('repository') != REPOSITORY or settings.get('paid_features_enabled') is not False
            or settings.get('visibility_change_requested') is not False
            or settings.get('development_policy', {}).get('branch_protection_requested') is not False):
        raise publisher.PublishError('Preserve the private/free/no-protection repository policy.')
    description = settings.get('description')
    topics = settings.get('topics')
    if (not isinstance(description, str) or not 1 <= len(description) <= 350
            or not isinstance(topics, list) or not 1 <= len(topics) <= 20
            or any(not isinstance(value, str) or not re.fullmatch('[a-z0-9-]{1,50}', value) for value in topics)
            or len(set(topics)) != len(topics)):
        raise publisher.PublishError('Review the description/topics before applying settings.')
    repository = client.api('repos/' + REPOSITORY)
    if repository.get('full_name') != REPOSITORY or repository.get('private') is not True:
        raise publisher.PublishError('The authorized application repository must remain private.')
    values = {'description': description, 'homepage': 'https://jdiller-86.github.io/CTR-2-WUP-docs/',
              'has_discussions': True, 'delete_branch_on_merge': True}
    result = {'ok': True, 'applied': apply, 'repository': REPOSITORY,
              'private': True, 'paid_features_enabled': False, 'branch_protection_changed': False,
              'requested_features': ['About/documentation URL', 'topics', 'Discussions', 'merged-branch cleanup', 'Dependabot alerts']}
    if not apply:
        return result
    client.api('repos/' + REPOSITORY, 'PATCH', values)
    client.api('repos/' + REPOSITORY + '/topics', 'PUT', {'names': topics})
    client.api('repos/' + REPOSITORY + '/vulnerability-alerts', 'PUT')
    verified = client.api('repos/' + REPOSITORY)
    if (verified.get('private') is not True or any(verified.get(key) != value for key, value in values.items())
            or set(verified.get('topics', [])) != set(topics)):
        raise publisher.PublishError('Repository feature verification did not match the reviewed settings.')
    # This endpoint succeeds only when alerts are enabled (204), otherwise 404.
    client.api('repos/' + REPOSITORY + '/vulnerability-alerts')
    result['live_settings_verified'] = True
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        result = configure(publisher.GitHub(), json.loads((ROOT / '.github/repository-settings.json').read_text()), args.apply)
    except (publisher.PublishError, OSError, ValueError, KeyError, TypeError) as error:
        result = {'ok': False, 'error': str(error) if isinstance(error, publisher.PublishError) else 'Repository configuration could not be verified.'}
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    return 0 if result['ok'] else 2


if __name__ == '__main__':
    raise SystemExit(main())

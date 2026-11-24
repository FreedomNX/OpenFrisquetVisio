"""Exercer les releases automatiques dans un dépôt Git isolé avec un faux gh."""

from datetime import datetime
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parents[1]
subprocess.run([sys.executable, ROOT / 'tools/changelog.py', 'check'], check=True)

with tempfile.TemporaryDirectory(prefix='ofv-changelog-') as directory:
    root = Path(directory)
    (root / 'tools').mkdir()
    shutil.copy2(ROOT / 'tools/changelog.py', root / 'tools/changelog.py')
    source = root / 'CHANGELOG.md'
    original = '# Notes\n\n## Notes intégrées à main\n\n### Ajouts\n\n- Première fonction.\n\n### Corrections\n\n### Changements\n'
    source.write_text(original, encoding='utf-8')
    environment = dict(os.environ)
    for key in ('GITHUB_SHA', 'GITHUB_REF', 'GITHUB_EVENT_NAME'):
        environment.pop(key, None)

    def git(*args):
        return subprocess.check_output(['git', *args], cwd=root, text=True).strip()

    def commit():
        git('add', 'CHANGELOG.md')
        git('-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
            'commit', '--quiet', '-m', 'Notes de test')
        return git('rev-parse', 'HEAD')

    def cli(*args, success=True, env=None):
        result = subprocess.run([sys.executable, 'tools/changelog.py', *args], cwd=root,
                                env=env or environment, text=True, capture_output=True)
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        return result.stdout

    git('init', '--quiet')
    git('init', '--quiet', '--bare', str(root / 'remote.git'))
    git('remote', 'add', 'origin', str(root / 'remote.git'))
    first_commit = commit()
    # Horloge Paris, version reproductible, sans préparation ni sortie publish.
    assert cli('ci', '--timestamp', '2026-11-24T09:55:00Z') == 'version=26.11.24.1055\n'
    assert cli('ci', '--timestamp', '2026-07-01T08:00:00Z') == 'version=26.07.01.1000\n'
    cli('ci', '--timestamp', '2026-11-24T09:55:00', success=False)
    now_before = datetime.now(ZoneInfo('Europe/Paris')).strftime('%y.%m.%d.%H%M')
    version = cli('ci').strip().split('=')[1]
    assert now_before <= version <= datetime.now(ZoneInfo('Europe/Paris')).strftime('%y.%m.%d.%H%M')

    # Archivage local sans mutation de la source, catégories vides omises.
    first = root / 'first.md'
    cli('archive', '26.11.24.1055', '--output', str(first))
    assert source.read_text() == original
    assert '## 26.11.24.1055 — 2026-11-24' in first.read_text()
    assert '### Corrections' not in first.read_text()
    baseline = root / 'baseline.md'
    baseline.write_text(original, encoding='utf-8')
    updated = original.replace('### Corrections', '### Corrections\n\n- Deuxième correction\n  sur plusieurs lignes.', 1)
    source.write_text(updated, encoding='utf-8')
    second = root / 'second.md'
    cli('archive', '26.11.24.1056', '--previous', str(first), '--baseline', str(baseline), '--output', str(second))
    assert second.read_text().count('Première fonction') == 1
    assert second.read_text().count('Deuxième correction') == 1
    assert 'Première fonction' not in second.read_text().split('## 26.11.24.1055')[0]
    third = root / 'third.md'
    baseline.write_text(updated, encoding='utf-8')
    cli('archive', '26.11.24.1057', '--previous', str(second), '--baseline', str(baseline), '--output', str(third))
    assert 'sans nouvelle note' in third.read_text().split('## 26.11.24.1056')[0]
    assert source.read_text() == updated
    for invalid in ('26.02.30.1200', '26.11.24.2500', '26.11.24.0955\npublish=true', 'v1.0.0'):
        cli('archive', invalid, '--output', str(third), success=False)
        assert source.read_text() == updated
    cli('archive', '26.11.24.1055', '--previous', str(first), '--output', str(third), success=False)
    # Les anciennes sources « À venir » restent lisibles pour une migration.
    source.write_text(original.replace('Notes intégrées à main', 'À venir'), encoding='utf-8')
    cli('check')
    source.write_text(original, encoding='utf-8')

    mockbin = root / 'mockbin'
    mockbin.mkdir()
    state_file = root / 'gh-state.json'
    state_file.write_text(json.dumps({'releases': [], 'calls': []}))
    gh = mockbin / 'gh'
    gh.write_text('''#!/usr/bin/env python3
import json, os, pathlib, subprocess, sys
path = pathlib.Path('gh-state.json')
s = json.loads(path.read_text())
a = sys.argv[1:]
s['calls'].append(a)
path.write_text(json.dumps(s))
def save(): path.write_text(json.dumps(s))
if a[0] == 'api':
    route = next(x for x in a if x.startswith('repos/'))
    if route.endswith('/releases'): print(json.dumps([s['releases']]))
    elif '/actions/runs/' in route: print('2026-11-24T09:55:00Z')
    elif '/commits/' in route:
        tag = route.rsplit('/', 1)[1]
        print(next(r['target_commitish'] for r in s['releases'] if r['tag_name'] == tag))
    else: print(os.environ['GITHUB_SHA'])
    sys.exit(0)
assert a[0] == 'release'
if a[1] == 'download':
    if os.environ.get('FAIL_DOWNLOAD'): sys.exit(1)
    r = next(r for r in s['releases'] if r['tag_name'] == a[2])
    dest = pathlib.Path(a[a.index('--dir')+1]); dest.mkdir(exist_ok=True)
    for asset in r['assets']:
        if asset['name'] in ['CHANGELOG.md','changelog-source.md']:
            (dest/asset['name']).write_text(asset['content'])
    sys.exit(0)
if a[1] == 'create':
    assert '--draft' in a
    r = {'tag_name': a[2], 'target_commitish': a[a.index('--target')+1],
         'draft': True, 'prerelease': False, 'assets': []}
    s['releases'].append(r)
else:
    r = next(r for r in s['releases'] if r['tag_name'] == a[2])
    assert r['draft'], 'Une release publiée ne doit jamais être écrasée'
if a[1] in ['create','upload']:
    for file in a[3:]:
        if file.startswith('--'): break
        p = pathlib.Path(file)
        if p.is_file() and (p.suffix in ['.bin','.md'] or p.name == 'SHA256SUMS'):
            r['assets'] = [asset for asset in r['assets'] if asset['name'] != p.name]
            r['assets'].append({'name':p.name, 'content':p.read_text()})
if '--notes-file' in a: r['body'] = pathlib.Path(a[a.index('--notes-file')+1]).read_text()
if '--draft=false' in a:
    if os.environ.get('FAIL_PUBLISH'): save(); sys.exit(1)
    assert len(r['assets']) == 7
    r['draft'] = False
    r['latest'] = '--latest=true' in a
    subprocess.run(['git','tag',r['tag_name'],r['target_commitish']],check=True)
save()
''', encoding='utf-8')
    gh.chmod(0o755)
    artifacts = root / 'firmware'
    artifacts.mkdir()
    for name in ('heltec_wifi_lora_32_V3', 'heltec_wifi_lora_32_V4', 'heltec_wifi_lora_32_V4_3', 'ttgo_lora32_v21'):
        (artifacts / ('firmware-' + name + '.bin')).write_text('firmware de test')
    (artifacts / 'SHA256SUMS').write_text('sommes de test\n')
    environment.update(PATH=str(mockbin) + os.pathsep + os.environ['PATH'], GH_REPO='test/ofv',
                       GITHUB_SHA=first_commit, GITHUB_SERVER_URL='https://github.com', GITHUB_RUN_ID='123')

    def publish(version, **options):
        return cli('publish', version, '--artifacts', str(artifacts), '--output', str(root / 'output'), **options)

    def state():
        return json.loads(state_file.read_text())

    publish('26.11.24.1055')
    released = state()['releases'][0]
    assert not released['draft'] and released['latest']
    assert len(released['assets']) == 7
    assert released['body'].count('Première fonction') == 1
    assert 'boot_app0' in released['body'] and 'SHA256SUMS' in released['body']
    assert source.read_text() == original
    saved = state()['releases']
    assert 'déjà publiée' in publish('26.11.24.1055')
    assert state()['releases'] == saved  # Relance : aucun fichier publié réécrit.
    assert cli('ci', '--timestamp', '2026-11-24T09:55:00Z') == 'version=26.11.24.1055\n'

    # Deux commits dans la même minute reçoivent des versions distinctes.
    source.write_text(updated + '\n- Texte littéral $(touch intrus).\n', encoding='utf-8')
    second_commit = commit()
    environment['GITHUB_SHA'] = second_commit
    assert cli('ci', '--timestamp', '2026-11-24T09:55:00Z') == 'version=26.11.24.1056\n'
    publish('26.11.24.1055', success=False)  # Tag déjà utilisé pour un autre commit.
    # Échec réseau : aucune nouvelle release n'est rendue publique.
    publish('26.11.24.1056', success=False, env=dict(environment, FAIL_DOWNLOAD='1'))
    assert state()['releases'] == saved
    publish('26.11.24.1056', success=False, env=dict(environment, FAIL_PUBLISH='1'))
    assert state()['releases'][-1]['draft']
    publish('26.11.24.1056')  # Reprise de notre brouillon seulement.
    latest = state()['releases'][-1]
    assert not latest['draft'] and 'Deuxième correction' in latest['body']
    assert 'Première fonction' not in latest['body']
    assert '$(touch intrus)' in latest['body'] and not (root / 'intrus').exists()
    history = next(asset['content'] for asset in latest['assets'] if asset['name'] == 'CHANGELOG.md')
    assert history.count('Première fonction') == 1 and history.count('Deuxième correction') == 1
    assert cli('ci', '--timestamp', '2026-11-24T09:55:00Z') == 'version=26.11.24.1056\n'

    # Un push sans note nouvelle publie tout de même sa version.
    git('-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
        'commit', '--allow-empty', '--quiet', '-m', 'Sans note supplémentaire')
    environment['GITHUB_SHA'] = git('rev-parse', 'HEAD')
    publish('26.11.24.1057')
    assert 'sans nouvelle note' in state()['releases'][-1]['body']
    saved = state()['releases']
    (artifacts / 'SHA256SUMS').unlink()
    publish('26.11.24.1058', success=False)
    assert state()['releases'] == saved

    # Exécuter aussi les vraies étapes shell de la CI avec ce faux gh.
    workflow = (ROOT / '.github/workflows/ci.yml').read_text(encoding='utf-8')
    def shell(name):
        step = workflow.split('      - name: ' + name + '\n', 1)[1]
        return textwrap.dedent(re.search(r'        run: \|\n((?: {10}[^\n]*\n|\n)+)', step).group(1))
    environment.update(GITHUB_OUTPUT=str(root / 'github-output'), GITHUB_REPOSITORY='test/ofv')
    subprocess.run(['bash', '-e', '-c', shell('Définir une version commune aux quatre cartes')],
                   cwd=root, env=environment, check=True)
    assert (root / 'github-output').read_text() == 'version=26.11.24.1057\n'
    assert 'needs.version.outputs.publish' not in workflow
    assert 'PUBLISH_RELEASE' not in workflow
    assert 'before' not in shell('Définir une version commune aux quatre cartes')
    assert '--artifacts firmware' in shell('Publier dans GitHub Releases')

print('OK: publication automatique, notes différentielles, historique, relances, brouillons, collisions et échecs sans perte')

"""Fetch exact upstream revisions and their pinned runtime submodules."""
import json
from pathlib import Path
import subprocess
import shutil

ROOT = Path(__file__).resolve().parents[1]
URLS = {'forgebox': 'https://github.com/KeystoneHQ/forgebox-helloworld.git'}

def git(*args):
    subprocess.run(['git', '-c', 'core.longpaths=true', *map(str, args)], check=True)

def main():
    lock = json.loads((ROOT / 'dependencies.lock.json').read_text(encoding='utf-8-sig'))
    (ROOT / 'vendor').mkdir(exist_ok=True)
    for name, url in URLS.items():
        path = ROOT / 'vendor' / name
        fresh = not path.exists()
        if fresh:
            git('clone', '--no-checkout', '--depth', '1', url, path)
        if not (path / '.git').exists():
            raise RuntimeError('Not an upstream checkout: %s' % path)
        if not fresh:
            dirty = subprocess.check_output(['git', '-C', str(path), 'status', '--porcelain',
                                             '--untracked-files=no'], text=True)
            if dirty.strip():
                raise RuntimeError('Refusing to change a modified dependency: %s' % path)
        git('-C', path, 'fetch', '--depth', '1', 'origin', lock[name])
        git('-C', path, 'checkout', '--detach', lock[name])
    # Use the runtime submodule of this fork checkout.
    git('-C', ROOT.parent, 'submodule', 'update', '--init', '--recursive', 'f469-disco')
    specter = ROOT / 'vendor/specter'
    specter.mkdir(exist_ok=True)
    runtime = specter / 'f469-disco'
    shutil.copytree(ROOT.parent / 'f469-disco', runtime, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('.git'))


if __name__ == '__main__':
    main()

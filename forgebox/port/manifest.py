import os
def tree(root, excluded=()):
    files = []
    for base, dirs, names in os.walk(root):
        dirs[:] = sorted(d for d in dirs if d not in excluded)
        files += [os.path.relpath(os.path.join(base, n), root).replace(os.sep, '/')
                  for n in sorted(names) if n.endswith('.py')]
    freeze(root, tuple(files))
tree('../build/app')
tree('../vendor/specter/f469-disco/libs/common', ('embit',))
tree('../vendor/specter/f469-disco/libs/common/embit/src', ('util',))

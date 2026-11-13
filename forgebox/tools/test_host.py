"""Build and run the port on a host CPU, using its actual MicroPython/LVGL code."""
from pathlib import Path
import os
import subprocess
from build import ROOT, prepare, run

os.chdir(ROOT)
disco = prepare()
run('make', '-C', str(disco / 'micropython/mpy-cross'), '-j4', 'CFLAGS_EXTRA=-Wno-error')
run('make', '-C', 'port', 'HOST=1', '../build/host/genhdr/qstrdefs.generated.h')
(ROOT / 'build/host/frozen_content.c').unlink(missing_ok=True)
graphics = disco / 'usermods/udisplay_f469'
sources = sorted((graphics / 'lvgl/src').rglob('*.c'))
sources += sorted((graphics / 'fonts').glob('*.c'))
sources += [graphics / 'pixelart/px_img.c']
out = ROOT / 'build/host-graphics'
out.mkdir(exist_ok=True)
objects = []
from concurrent.futures import ThreadPoolExecutor
def compile_one(pair):
    i, source = pair
    target = out / ('%d.o' % i)
    newest = max(source.stat().st_mtime, (ROOT / 'port/mpconfigport.h').stat().st_mtime,
                 (ROOT / 'build/host/genhdr/qstrdefs.generated.h').stat().st_mtime)
    if not target.exists() or target.stat().st_mtime < newest:
        run('gcc', '-c', '-Os', '-DFORGEBOX_HOST=1', '-Iport',
            '-Ibuild/host', '-I' + str(disco / 'micropython'), '-I' + str(graphics),
            '-o', str(target), str(source))
    return str(target)
with ThreadPoolExecutor(max_workers=4) as pool:
    objects = list(pool.map(compile_one, enumerate(sources)))
run('ar', 'rcs', 'build/host_graphics.a', *objects)
run('make', '-C', 'port', 'HOST=1', '-j4')
run('build/host/specter-host')

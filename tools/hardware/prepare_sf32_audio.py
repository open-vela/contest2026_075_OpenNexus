"""Enable the FocusMate audio Kconfig profile in an existing SF32LB52 build.

Apply patches/board/audio-ui-board.patch first. This script changes generated
build configuration only; it does not flash or download dependencies.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('workspace', type=Path, help='openvela workspace containing nuttx, apps, vendor')
parser.add_argument('--build', default='cmake_out/sf32lb52_devkit_lcd')
args = parser.parse_args()
workspace = args.workspace.resolve()
build = (workspace / args.build).resolve()
if not build.is_relative_to(workspace):
    parser.error('build must stay inside the selected workspace')
config = build / '.config'
if not config.is_file() or 'CONFIG_ARCH_CHIP_SF32LB52=y' not in config.read_text():
    parser.error('initialize the SF32LB52 CMake build before enabling audio')
backup = build / '.config.pre-focusmate-audio'
if not backup.exists():
    shutil.copyfile(config, backup)
symbols = {
    'CONFIG_AUDIO': 'y', 'CONFIG_AUDIO_FORMAT_PCM': 'y',
    'CONFIG_AUDIO_DRIVER_SPECIFIC_BUFFERS': 'y',
    'CONFIG_AUDIO_NUM_BUFFERS': '4', 'CONFIG_AUDIO_BUFFER_NUMBYTES': '2048',
    'CONFIG_FOCUSMATE_HOME_MASK': '0x1', 'CONFIG_FOCUSMATE_TALK_MASK': '0x2',
}
lines = config.read_text().splitlines()
for name, value in symbols.items():
    lines = [line for line in lines if not (line.startswith(name+'=') or line == '# '+name+' is not set')]
    lines.append(name+'='+value)
config.write_text('\n'.join(lines)+'\n')

# Some existing contest workspaces have generated menu references for absent
# optional vendors. Remove only those generated references, preserving copies.
omitted = []
for path in (build / 'apps').rglob('*Kconfig*'):
    if path.name.endswith('.pre-focusmate-audio') or not path.is_file() or not path.resolve().is_relative_to(build):
        continue
    text = path.read_text()
    kept = []
    for line in text.splitlines():
        match = re.match(r'\s*source\s+"(/[^"]+)"', line)
        if match and not Path(match[1]).exists():
            omitted.append(match[1])
        else:
            kept.append(line)
    if len(kept) != len(text.splitlines()):
        prior = path.with_name(path.name+'.pre-focusmate-audio')
        if not prior.exists():
            prior.write_text(text)
        path.write_text('\n'.join(kept)+'\n')
env = os.environ.copy()
env['PATH'] = str(Path.home()/'.local/bin')+os.pathsep+env['PATH']
env.update(KCONFIG_CONFIG=str(config), EXTERNALDIR='dummy', APPSDIR=str(workspace/'apps'),
           DRIVERS_PLATFORM_DIR='dummy', APPSBINDIR=str(build/'apps'), BINDIR=str(build))
tool = shutil.which('olddefconfig', path=env['PATH'])
if not tool:
    parser.error('olddefconfig is unavailable; use the existing workspace Kconfig tool environment')
with (build/'focusmate-audio-config.log').open('w') as log:
    subprocess.run([tool], cwd=workspace/'nuttx', env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
print('Audio configuration enabled. Missing generated optional menus omitted:', len(omitted))
print('Next: cmake -B '+str(build)+' -S '+str(workspace/'nuttx')+
      ' -DFOCUSMATE_PHONE_GPIO=ON -DFOCUSMATE_BOARD_AUDIO=ON -DFOCUSMATE_MIC_TEST=OFF')

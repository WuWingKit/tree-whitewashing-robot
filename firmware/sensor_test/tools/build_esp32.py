"""Build native ESP-IDF sensor test, staging under an ASCII path on Windows.

Requires ESP-IDF v5.4.2 (including its Python environment/toolchain) and
espressif/esp-usb commit 51fcfb96417f8634f0117591d76b1aca645ef185.
Run from an exported ESP-IDF terminal; no boards are accessed.
"""
from pathlib import Path
import argparse
import os
import json
import shutil
import subprocess
import runpy
import hashlib

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--idf', type=Path, default=Path(os.environ.get('IDF_PATH', 'E:/espwork/tree_robot_v3/esp-idf')))
    p.add_argument('--uvc', type=Path, default=Path('E:/espwork/tree_robot_v3/esp-usb/host/class/uvc/usb_host_uvc'))
    p.add_argument('--work', type=Path, default=Path('E:/espwork/tree_robot_v3/project'))
    p.add_argument('--python', default='E:/espwork/tree_robot_v3/idf_tools/python_env/idf5.4_py3.12_env/Scripts/python.exe')
    p.add_argument('--tool-bin', type=Path, default=Path('E:/espwork/tree_robot_v3/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin'))
    p.add_argument('--cmake', default='E:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
    p.add_argument('--ninja', type=Path, default=Path('E:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'))
    p.add_argument('--profile', choices=['legacy','pcb_v1','pcb_v2'], required=True, help='must be explicit: legacy / pcb_v1 / pcb_v2')
    args = p.parse_args()
    profile_source=ROOT.parent/'board_profiles/pinmaps.json'
    runpy.run_path(str(ROOT.parent/'board_profiles/generate.py'))['generate'](check=True)
    if not (args.uvc / 'uvc_host.c').is_file():
        p.error('Missing pinned usb_host_uvc source; see SOURCES.md')
    shutil.copytree(ROOT / 'esp32', args.work, dirs_exist_ok=True, ignore=shutil.ignore_patterns('build', 'sdkconfig', 'sdkconfig.old'))
    shutil.copytree(ROOT.parent / 'remote_control/common', args.work / 'common', dirs_exist_ok=True)
    main_cmake = args.work / 'main/CMakeLists.txt'
    main_cmake.write_text(main_cmake.read_text(encoding='utf-8').replace('../../../remote_control/common', '../common'), encoding='utf-8')
    env = os.environ.copy()
    env.update(IDF_PATH=str(args.idf), TREE_UVC_PATH=str(args.uvc), IDF_COMPONENT_MANAGER='0',
               IDF_PYTHON_ENV_PATH=str(Path(args.python).parent.parent),
               IDF_TOOLS_PATH=str(args.idf.parent / 'idf_tools'))
    env['PATH'] = os.pathsep.join([str(args.tool_bin), str(Path(args.python).parent), str(args.ninja.parent), env['PATH']])
    build = args.work / 'build'
    subprocess.run([args.cmake, '-S', str(args.work), '-B', str(build), '-G', 'Ninja',
                    '-DIDF_TARGET=esp32s3', '-DPYTHON='+args.python,
                    '-DROBOT_WIRING_PROFILE='+str({'legacy':0,'pcb_v1':1,'pcb_v2':2}[args.profile])], env=env, check=True)
    subprocess.run([str(args.ninja), '-C', str(build), '-j', '6'], env=env, check=True)
    out = ROOT / 'build/esp32' if args.profile=='legacy' else ROOT / 'build/esp32' / args.profile
    out.mkdir(parents=True, exist_ok=True)
    for src, dest in [('tree_robot_sensor_test.bin', 'app.bin'), ('bootloader/bootloader.bin', 'bootloader.bin'),
                      ('partition_table/partition-table.bin', 'partitions.bin')]:
        shutil.copy2(build / src, out / dest)
    flash = json.loads((build / 'flasher_args.json').read_text(encoding='utf-8'))
    flash['flash_files'] = {'0x0': 'bootloader.bin', '0x8000': 'partitions.bin', '0x10000': 'app.bin'}
    for section, name in [('bootloader', 'bootloader.bin'), ('partition-table', 'partitions.bin'), ('app', 'app.bin')]:
        if section in flash:
            flash[section]['file'] = name
    (out / 'flasher_args.json').write_text(json.dumps(flash, indent=2), encoding='utf-8')
    (out/'wiring_profile.json').write_text(json.dumps(dict(profile=args.profile,
        pinmaps_sha256=hashlib.sha256(profile_source.read_bytes()).hexdigest(),
        pins=json.loads(profile_source.read_text(encoding='utf-8'))['profiles'][args.profile],
        device_flashed=False),indent=2),encoding='utf-8')
    print('Compiled only. No device flashed. Output:', out)


if __name__ == '__main__':
    main()

"""Build the existing remote-control/OTA sketch plus USB camera/ToF support.

Requires ESP-IDF v5.4.2 (including its Python environment/toolchain) and
espressif/esp-usb commit 51fcfb96417f8634f0117591d76b1aca645ef185.
Run from an exported ESP-IDF terminal; no boards are accessed.
"""
from pathlib import Path
import argparse
import os
import json
import hashlib
import re
import shutil
import subprocess
import runpy

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--idf', type=Path, default=Path(os.environ.get('IDF_PATH', 'E:/espwork/tree_robot_v3/esp-idf')))
    p.add_argument('--arduino', type=Path, default=Path('E:/espwork/tree_robot_v3/arduino'))
    p.add_argument('--uvc', type=Path, default=Path('E:/espwork/tree_robot_v3/esp-usb/host/class/uvc/usb_host_uvc'))
    p.add_argument('--work', type=Path, default=Path('E:/espwork/tree_robot_v3/integrated'))
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
    from prepare import prepare_sketch
    prepare_sketch()
    shutil.copytree(ROOT / 'esp32/idf', args.work, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('build', 'sdkconfig', 'sdkconfig.old'))
    sketch = ROOT / 'esp32/RemoteControl'
    for name in ['RemoteControl.ino', 'ota_boot.cpp', 'ota_support.h', 'ota_config.h', 'ota_ui.h', 'web_ui.h', 'sensor_support.h', 'upper_support.h', 'robot_pinmap.h']:
        shutil.copy2(sketch / name, args.work / 'main' / ('RemoteControl.cpp' if name.endswith('.ino') else name))
    shutil.copytree(sketch / 'src', args.work / 'main/src', dirs_exist_ok=True)
    sensor = ROOT.parent / 'sensor_test/esp32/main'
    for name in ['cameras.c', 'cameras.h']:
        shutil.copy2(sensor / name, args.work / 'main' / name)
    page = (sensor / 'index.html').read_text(encoding='utf-8')
    page = page.replace('/api/status', '/api/sensors/status')
    page = page.replace('本固件只采集传感器，持续发送停车指令。双摄像头接入后，选择一路采集并切换测试。',
                        '遥控、OTA、测距和摄像头已整合。双摄像头选择一路采集；遥控请在原主页操作。')
    (args.work / 'main/index.html').write_text(page, encoding='utf-8')
    env = os.environ.copy()
    env.update(IDF_PATH=str(args.idf), TREE_UVC_PATH=str(args.uvc), TREE_ARDUINO_PATH=str(args.arduino), IDF_COMPONENT_MANAGER='0',
               IDF_PYTHON_ENV_PATH=str(Path(args.python).parent.parent),
               IDF_TOOLS_PATH=str(args.idf.parent / 'idf_tools'))
    env['PATH'] = os.pathsep.join([str(args.tool_bin), str(Path(args.python).parent), str(args.ninja.parent), env['PATH']])
    build = args.work / 'build'
    subprocess.run([args.cmake, '-S', str(args.work), '-B', str(build), '-G', 'Ninja',
                    '-DIDF_TARGET=esp32s3', '-DPYTHON='+args.python,
                    '-DROBOT_WIRING_PROFILE='+str({'legacy':0,'pcb_v1':1,'pcb_v2':2}[args.profile])], env=env, check=True)
    subprocess.run([str(args.ninja), '-C', str(build), '-j', '6'], env=env, check=True)
    out = ROOT / 'esp32/build/integrated' if args.profile=='legacy' else ROOT / 'esp32/build/integrated' / args.profile
    out.mkdir(parents=True, exist_ok=True)
    for src, dest in [('tree_robot_remote_control.bin', 'app.bin'), ('bootloader/bootloader.bin', 'bootloader.bin'),
                      ('partition_table/partition-table.bin', 'partitions.bin')]:
        shutil.copy2(build / src, out / dest)
    flash = json.loads((build / 'flasher_args.json').read_text(encoding='utf-8'))
    (out / 'reset_otadata.bin').write_bytes(b'\xff'*8192)
    flash['flash_files'] = {'0x0': 'bootloader.bin', '0x8000': 'partitions.bin', '0xe000': 'reset_otadata.bin', '0x10000': 'app.bin'}
    for section, name in [('bootloader', 'bootloader.bin'), ('partition-table', 'partitions.bin'), ('app', 'app.bin')]:
        if section in flash:
            flash[section]['file'] = name
    (out / 'flasher_args.json').write_text(json.dumps(flash, indent=2), encoding='utf-8')
    image = (out/'app.bin').read_bytes()
    if len(image)>0x140000 or image[0]!=0xe9 or image[12:14]!=b'\x09\x00' or image[32:36]!=b'\x32\x54\xcd\xab' or b'TREE_ROBOT_S3_RC_OTA_V1' not in image:
        raise SystemExit('Image incompatible with original OTA checks/slot size')
    fw_version = re.search(r'FW_VERSION="([^"]+)"', (sketch/'ota_support.h').read_text(encoding='utf-8')).group(1)
    (out/'ota.json').write_text(json.dumps(dict(target='tree-robot-esp32s3',version=fw_version,
        size=len(image),sha256=hashlib.sha256(image).hexdigest(),file='app.bin'),indent=2),encoding='utf-8')
    (out/'wiring_profile.json').write_text(json.dumps(dict(profile=args.profile,
        pinmaps_sha256=hashlib.sha256(profile_source.read_bytes()).hexdigest(),
        pins=json.loads(profile_source.read_text(encoding='utf-8'))['profiles'][args.profile],
        device_flashed=False),indent=2),encoding='utf-8')
    print('Compiled only. No device flashed. Output:', out)


if __name__ == '__main__':
    main()

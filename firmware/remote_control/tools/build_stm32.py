"""Build with installed ARM Compiler 5; no Keil GUI or board required."""
from pathlib import Path
import argparse
import subprocess
import json
import hashlib
import runpy
from prepare import ROOT, HAL_NAMES, CAMERA_HAL, USB_HOST_CORE, enable_full_speed_isoc

def build(keil, profile):
    runpy.run_path(str(ROOT.parent/"board_profiles/generate.py"))["generate"](check=True)
    tools = keil/"ARM/ARMCC/bin"
    profiles = ROOT.parents[0]/"board_profiles/pinmaps.json"
    pinmaps = json.loads(profiles.read_text(encoding="utf-8"))
    assert profile in pinmaps["profiles"]
    profile_number = {"legacy": 0, "pcb_v1": 1, "pcb_v2": 2}[profile]
    out = ROOT/"stm32/build" if profile == "legacy" else ROOT/"stm32/build"/profile
    out.mkdir(parents=True, exist_ok=True)
    includes = [ROOT/"stm32/App", ROOT/"common", ROOT/"stm32/.vendor/CORE",
                ROOT/"stm32/.vendor/USER", ROOT/"stm32/.vendor/HALLIB/STM32F4xx_HAL_Driver/Inc"]
    sources = [ROOT/"stm32/App/main.c", ROOT/"stm32/App/tof.c", ROOT/"common/rc_protocol.c", ROOT/"common/rc_control.c",
               ROOT/"stm32/.vendor/USER/system_stm32f4xx.c"]
    sources += [ROOT/"stm32/.vendor/HALLIB/STM32F4xx_HAL_Driver/Src"/(s+".c") for s in HAL_NAMES]
    if profile == "pcb_v2":
        usb_host = ROOT/"stm32/.vendor/usb-host/Core"
        if not (usb_host/"Src/usbh_core.c").is_file():
            raise SystemExit("Run firmware/remote_control/tools/prepare.py to install the pinned ST USB Host library")
        enable_full_speed_isoc(usb_host.parent)
        includes += [usb_host/"Inc"]
        sources += [usb_host/"Src"/(name+".c") for name in USB_HOST_CORE]
        sources += [ROOT/"stm32/.vendor/HALLIB/STM32F4xx_HAL_Driver/Src"/(s+".c") for s in CAMERA_HAL]
        sources += [ROOT/"stm32/App/camera_f407.c", ROOT/"stm32/App/camera_usb_ll.c", ROOT/"common/camera_link.c"]
    objects = []
    def run(args):
        subprocess.run([str(x) for x in args], check=True, cwd=out)
    for src in sources:
        obj = out/(src.stem+".o")
        args = [tools/"armcc.exe", "--cpu=Cortex-M4.fp", "--fpu=FPv4-SP", "--c99", "-O1",
                "--split_sections", "-g", "-DUSE_HAL_DRIVER", "-DSTM32F407xx",
                "-DROBOT_WIRING_PROFILE="+str(profile_number)]
        if profile == "pcb_v2":
            args += ["-DTREE_F407_UVC=1"]
        for inc in includes:
            args += ["-I", inc]
        run(args+["-c", src, "-o", obj])
        objects.append(obj)
    startup = out/"startup.o"
    run([tools/"armasm.exe", "--cpu=Cortex-M4.fp", "--fpu=FPv4-SP",
         ROOT/"stm32/.vendor/CORE/startup_stm32f407xx.s", "-o", startup])
    axf = out/"remote_control.axf"
    scatter = out/"remote_control.sct"
    scatter.write_text('LR_IROM1 0x08000000 0x00100000 {\n ER_IROM1 0x08000000 0x00100000 {\n startup.o (RESET, +First)\n *(InRoot$$Sections)\n .ANY (+RO)\n }\n RW_IRAM1 0x20000000 0x00020000 {\n .ANY (+RW +ZI)\n }\n}\n')
    run([tools/"armlink.exe", "--cpu=Cortex-M4.fp", "--scatter", scatter,
         "--entry=Reset_Handler", "--map", "--list", out/"remote_control.map", "-o", axf,
         startup]+objects)
    run([tools/"fromelf.exe", "--i32combined", "--output", out/"remote_control.hex", axf])
    run([tools/"fromelf.exe", "--bin", "--output", out/"remote_control.bin", axf])
    run([tools/"fromelf.exe", "--text", "-z", axf])
    (out/"wiring_profile.json").write_text(json.dumps({"profile":profile,
        "pinmaps_sha256":hashlib.sha256(profiles.read_bytes()).hexdigest(),
        "pins":pinmaps["profiles"][profile],"device_flashed":False},indent=2),encoding="utf-8")
    print("Built:", out/"remote_control.hex")

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--keil", type=Path, default=Path("E:/Keil_v5"))
    p.add_argument("--profile", choices=["legacy","pcb_v1","pcb_v2"], required=True, help="must be explicit: legacy / pcb_v1 / pcb_v2")
    args=p.parse_args()
    build(args.keil, args.profile)

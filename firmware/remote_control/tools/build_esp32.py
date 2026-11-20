"""Build with an installed Arduino CLI/core; keeps GCC paths short and ASCII."""
from pathlib import Path
import argparse
import shutil
import subprocess
import runpy
import json
import hashlib
from prepare import ROOT, prepare_sketch

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--arduino-cli",default="arduino-cli")
    p.add_argument("--work-dir",type=Path,required=True,help="Dedicated short ASCII build directory")
    p.add_argument("--profile", choices=["legacy","pcb_v1","pcb_v2"], required=True, help="must be explicit: legacy / pcb_v1 / pcb_v2")
    a=p.parse_args()
    source=ROOT.parent/"board_profiles/pinmaps.json"
    runpy.run_path(str(ROOT.parent/"board_profiles/generate.py"))["generate"](check=True)
    work=a.work_dir.resolve()
    if not str(work).isascii():raise SystemExit("Use an ASCII path, e.g. C:/espwork/tree_robot_rc")
    prepare_sketch()
    sketch=work/"RemoteControl"
    shutil.copytree(ROOT/"esp32/RemoteControl",sketch,dirs_exist_ok=True)
    out=ROOT/"esp32/build" if a.profile=="legacy" else ROOT/"esp32/build"/a.profile
    out.mkdir(parents=True,exist_ok=True)
    profile_number={"legacy":0,"pcb_v1":1,"pcb_v2":2}[a.profile]
    subprocess.run([a.arduino_cli,"compile","--fqbn",
        "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=default,CDCOnBoot=default",
        "--build-property","compiler.cpp.extra_flags=-DROBOT_WIRING_PROFILE="+str(profile_number),
        "--build-path",str(work/"build"/a.profile),"--output-dir",str(out),
        "--warnings","all",str(sketch)],check=True)
    (out/"wiring_profile.json").write_text(json.dumps(dict(profile=a.profile,
        pinmaps_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        pins=json.loads(source.read_text(encoding="utf-8"))["profiles"][a.profile],
        device_flashed=False),indent=2),encoding="utf-8")
    print("S3 binaries:",out)
if __name__=="__main__":main()

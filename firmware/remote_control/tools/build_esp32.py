"""Build with an installed Arduino CLI/core; keeps GCC paths short and ASCII."""
from pathlib import Path
import argparse
import shutil
import subprocess
from prepare import ROOT, prepare_sketch

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--arduino-cli",default="arduino-cli")
    p.add_argument("--work-dir",type=Path,required=True,help="Dedicated short ASCII build directory")
    a=p.parse_args()
    work=a.work_dir.resolve()
    if not str(work).isascii():raise SystemExit("Use an ASCII path, e.g. C:/espwork/tree_robot_rc")
    prepare_sketch()
    sketch=work/"RemoteControl"
    shutil.copytree(ROOT/"esp32/RemoteControl",sketch,dirs_exist_ok=True)
    subprocess.run([a.arduino_cli,"compile","--fqbn",
        "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=default,CDCOnBoot=default",
        "--build-path",str(work/"build"),"--output-dir",str(ROOT/"esp32/build"),
        "--warnings","all",str(sketch)],check=True)
    print("S3 binaries:",ROOT/"esp32/build")
if __name__=="__main__":main()

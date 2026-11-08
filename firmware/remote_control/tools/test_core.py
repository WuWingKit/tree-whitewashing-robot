"""Compile and run the actual shared C on Windows using MSVC (or --cc gcc)."""
from pathlib import Path
import argparse
import os
import subprocess
from prepare import ROOT

def main():
    p=argparse.ArgumentParser();p.add_argument("--cc", default="msvc");a=p.parse_args()
    out=ROOT/"stm32/build/tests";out.mkdir(parents=True,exist_ok=True)
    src=[ROOT/"tests/test_core.c",ROOT/"common/rc_protocol.c",ROOT/"common/rc_control.c"]
    exe=out/("test_core.exe" if os.name=="nt" else "test_core")
    env=os.environ.copy()
    if a.cc=="msvc":
        vswhere=Path(os.environ.get("ProgramFiles(x86)","C:/Program Files (x86)"))/"Microsoft Visual Studio/Installer/vswhere.exe"
        vs=Path(subprocess.check_output([str(vswhere),"-latest","-products","*","-property","installationPath"],text=True).strip())
        vcvars=vs/"VC/Auxiliary/Build/vcvars64.bat"
        settings=subprocess.check_output(f'call "{vcvars}" >nul && set',shell=True,text=True)
        for line in settings.splitlines():
            if '=' in line:
                key,value=line.split('=',1);env[key]=value
        compiler=next((vs/"VC/Tools/MSVC").glob("*/bin/Hostx64/x64/cl.exe"))
        cmd=[str(compiler),"/nologo","/W4","/WX","/std:c11","/TC","/I"+str(ROOT/"common")]+[str(s) for s in src]+["/Fe:"+str(exe)]
    else:
        cmd=[a.cc,"-std=c99","-Wall","-Wextra","-Werror","-I",str(ROOT/"common")]+[str(s) for s in src]+["-o",str(exe)]
    subprocess.run(cmd,cwd=out,env=env,check=True)
    subprocess.run([str(exe)],check=True)
if __name__=="__main__":main()

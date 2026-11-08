"""Build with installed ARM Compiler 5; no Keil GUI or board required."""
from pathlib import Path
import argparse
import subprocess
from prepare import ROOT, HAL_NAMES

def build(keil):
    tools = keil/"ARM/ARMCC/bin"
    out = ROOT/"stm32/build"
    out.mkdir(parents=True, exist_ok=True)
    includes = [ROOT/"stm32/App", ROOT/"common", ROOT/"stm32/.vendor/CORE",
                ROOT/"stm32/.vendor/USER", ROOT/"stm32/.vendor/HALLIB/STM32F4xx_HAL_Driver/Inc"]
    sources = [ROOT/"stm32/App/main.c", ROOT/"common/rc_protocol.c", ROOT/"common/rc_control.c",
               ROOT/"stm32/.vendor/USER/system_stm32f4xx.c"]
    sources += [ROOT/"stm32/.vendor/HALLIB/STM32F4xx_HAL_Driver/Src"/(s+".c") for s in HAL_NAMES]
    objects = []
    def run(args):
        subprocess.run([str(x) for x in args], check=True, cwd=out)
    for src in sources:
        obj = out/(src.stem+".o")
        args = [tools/"armcc.exe", "--cpu=Cortex-M4.fp", "--fpu=FPv4-SP", "--c99", "-O1",
                "--split_sections", "-g", "-DUSE_HAL_DRIVER", "-DSTM32F407xx"]
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
    print("Built:", out/"remote_control.hex")

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--keil", type=Path, default=Path("E:/Keil_v5"))
    build(p.parse_args().keil)

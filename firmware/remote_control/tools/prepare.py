"""Reuse the local ST HAL/CMSIS/startup, not the restricted vendor application.

Python 3, standard library only. Does not modify any original hardware material.
"""
from pathlib import Path
import argparse
import shutil
import json
import secrets
import re
import zipfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
HAL_NAMES = ["stm32f4xx_hal", "stm32f4xx_hal_cortex", "stm32f4xx_hal_gpio",
             "stm32f4xx_hal_rcc", "stm32f4xx_hal_rcc_ex", "stm32f4xx_hal_flash",
             "stm32f4xx_hal_flash_ex", "stm32f4xx_hal_pwr", "stm32f4xx_hal_pwr_ex",
             "stm32f4xx_hal_dma", "stm32f4xx_hal_tim", "stm32f4xx_hal_tim_ex",
             "stm32f4xx_hal_uart", "stm32f4xx_hal_iwdg"]

def prepare(materials):
    candidates = list(materials.rglob("3，标准例程-HAL库版本.zip"))
    if len(candidates) != 1:
        raise SystemExit("Expected one ALIENTEK HAL examples ZIP; use --materials to specify the directory.")
    vendor = ROOT / "stm32/.vendor"
    vendor.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(candidates[0]) as z:
        base = next(n.rsplit("USER/Template.uvprojx", 1)[0] for n in z.namelist()
                    if "实验0-1 " in n and n.endswith("USER/Template.uvprojx"))
        for n in z.namelist():
            if not n.startswith(base) or n.endswith("/"):
                continue
            rel = n[len(base):]
            allowed = (rel.startswith("CORE/") or
                       rel.startswith("HALLIB/STM32F4xx_HAL_Driver/Inc/") or
                       rel in ["USER/stm32f4xx.h", "USER/stm32f407xx.h",
                               "USER/system_stm32f4xx.h", "USER/system_stm32f4xx.c"] or
                       rel in ["HALLIB/STM32F4xx_HAL_Driver/Src/"+s+".c" for s in HAL_NAMES])
            if allowed:
                target = vendor / rel
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(z.read(n))
        tree = ET.fromstring(z.read(base+"USER/Template.uvprojx"))
    target = tree.find(".//Target")
    target.find("TargetName").text = "RemoteControl"
    common = target.find("TargetOption/TargetCommonOption")
    common.find("OutputDirectory").text = ".\\build\\"
    common.find("ListingPath").text = ".\\build\\"
    common.find("OutputName").text = "remote_control"
    common.find("BrowseInformation").text = "0"
    common.find("Cpu").text = 'IRAM(0x20000000,0x20000) IROM(0x08000000,0x100000) CPUTYPE("Cortex-M4") FPU2 CLOCK(8000000) ELITTLE'
    opts = target.find("TargetOption/TargetArmAds/Cads")
    opts.find("uC99").text = "1"
    inc = ["App", "..\\common", ".vendor\\CORE", ".vendor\\USER",
           ".vendor\\HALLIB\\STM32F4xx_HAL_Driver\\Inc"]
    opts.find("VariousControls/IncludePath").text = ";".join(inc)
    opts.find("VariousControls/Define").text = "USE_HAL_DRIVER,STM32F407xx"
    groups = target.find("Groups")
    groups.clear()
    source_groups = {
        "Application": ["App/main.c", "../common/rc_protocol.c", "../common/rc_control.c"],
        "ST CMSIS": [".vendor/CORE/startup_stm32f407xx.s", ".vendor/USER/system_stm32f4xx.c"],
        "ST HAL": [".vendor/HALLIB/STM32F4xx_HAL_Driver/Src/"+s+".c" for s in HAL_NAMES],
    }
    for name, sources in source_groups.items():
        group = ET.SubElement(groups, "Group")
        ET.SubElement(group, "GroupName").text = name
        files = ET.SubElement(group, "Files")
        for source in sources:
            f = ET.SubElement(files, "File")
            ET.SubElement(f, "FileName").text = Path(source).name
            ET.SubElement(f, "FileType").text = "2" if source.endswith(".s") else "1"
            ET.SubElement(f, "FilePath").text = source.replace("/", "\\")
    ET.indent(tree)
    ET.ElementTree(tree).write(ROOT/"stm32/RemoteControl.uvprojx", encoding="utf-8", xml_declaration=True)
    prepare_sketch()
    print("Prepared ST HAL/CMSIS, Keil project and Arduino sketch. Originals unchanged.")

def prepare_sketch():
    sketch = ROOT/"esp32/RemoteControl"
    (sketch/"src").mkdir(exist_ok=True)
    for name in ["rc_protocol.c", "rc_protocol.h", "rc_ota_image.c", "rc_ota_image.h"]:
        shutil.copy2(ROOT/"common"/name, sketch/"src"/name)
    html = (sketch/"index.html").read_text(encoding="utf-8")
    assert ')RCWEB"' not in html
    (sketch/"web_ui.h").write_text(
        '#pragma once\n#include <pgmspace.h>\nstatic const char WEB_UI[] PROGMEM=R"RCWEB(\n'+html+'\n)RCWEB";\n', encoding="utf-8")
    html=(sketch/"ota.html").read_text(encoding="utf-8")
    assert ')RCOTA"' not in html
    (sketch/"ota_ui.h").write_text('#pragma once\n#include <pgmspace.h>\nstatic const char OTA_UI[] PROGMEM=R"RCOTA(\n'+html+'\n)RCOTA";\n',encoding="utf-8")
    credentials=ROOT/"esp32/ota_access.json"
    if not credentials.exists():
        credentials.write_text(json.dumps({"username":"admin","password":secrets.token_urlsafe(24)},indent=2),encoding="utf-8")
    access=json.loads(credentials.read_text(encoding="utf-8"))
    assert access["username"]=="admin" and re.fullmatch(r"[A-Za-z0-9_-]{16,80}",access["password"])
    (sketch/"ota_config.h").write_text('#pragma once\n#define OTA_USERNAME "admin"\n#define OTA_PASSWORD "'+access["password"]+'"\n',encoding="utf-8")

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--materials", type=Path, default=REPO/"硬件资料")
    prepare(p.parse_args().materials)

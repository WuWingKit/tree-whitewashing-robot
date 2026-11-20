"""Read compiled STM32 ELF GPIO tables and verify all nine build sidecars.

This checks compiled direction tables, not hardware behavior or a live device.
"""
import hashlib
import json
import struct
from pathlib import Path

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
source=HERE/'pinmaps.json'
pinmaps=json.loads(source.read_text(encoding='utf-8'))
source_hash=hashlib.sha256(source.read_bytes()).hexdigest()

def elf_symbols(path):
    data=path.read_bytes()
    assert data[:6]==b'\x7fELF\x01\x01','Expected little-endian ELF32'
    header=struct.unpack_from('<16sHHIIIIIHHHHHH',data)
    sections=[struct.unpack_from('<10I',data,header[6]+i*header[11])for i in range(header[12])]
    symbols={}
    for section in sections:
        if section[1]!=2:continue
        strings=sections[section[6]]
        names=data[strings[4]:strings[4]+strings[5]]
        for pos in range(section[4],section[4]+section[5],section[9]):
            name,address,size,info,other,index=struct.unpack_from('<IIIBBH',data,pos)
            if not name or not size or not 0<index<len(sections):continue
            text=names[name:names.index(b'\0',name)].decode('ascii')
            owner=sections[index]
            if owner[1]==8:continue
            offset=owner[4]+address-owner[3]
            symbols[text]=data[offset:offset+size]
    return symbols

rows=[]
for profile in ['legacy','pcb_v1','pcb_v2']:
    suffix=Path()if profile=='legacy'else Path(profile)
    stm=ROOT/'firmware/remote_control/stm32/build'/suffix
    syms=elf_symbols(stm/'remote_control.axf')
    motors=pinmaps['profiles'][profile]['motor']
    for number in [1,2]:
        gpios=[m['in'+str(number)]for m in motors]
        addresses=[0x40020000+0x400*(ord(p[1])-ord('A'))for p in gpios]
        masks=[1<<int(p[2:])for p in gpios]
        assert syms['dir'+str(number)]==struct.pack('<4I',*addresses),(profile,'port table')
        assert syms['pins'+str(number)]==struct.pack('<4H',*masks),(profile,'pin mask table')
    for target,folder,file in [
        ('stm32',stm,'remote_control.bin'),
        ('esp32-integrated',ROOT/'firmware/remote_control/esp32/build/integrated'/suffix,'app.bin'),
        ('esp32-sensor-test',ROOT/'firmware/sensor_test/build/esp32'/suffix,'app.bin')]:
        metadata=json.loads((folder/'wiring_profile.json').read_text(encoding='utf-8'))
        assert metadata['profile']==profile
        assert metadata['pinmaps_sha256']==source_hash
        assert metadata['pins']==pinmaps['profiles'][profile]
        assert metadata['device_flashed']is False
        binary=(folder/file).read_bytes()
        rows.append(dict(target=target,profile=profile,path=str((folder/file).relative_to(ROOT)),
            bytes=len(binary),sha256=hashlib.sha256(binary).hexdigest(),
            compiled_motor_tables_checked=target=='stm32'))

report=dict(status='passed',pinmaps_sha256=source_hash,builds=rows,
    coverage=['ELF dir1/dir2 GPIO addresses and pins1/pins2 masks for all three STM32 profiles',
              'profile metadata, canonical pinmap hash and output identity for all nine binaries'],
    limitations=['ESP32 UART and STM32 ToF init checked by generated-source consistency and compilation, not binary disassembly',
                 'no hardware execution, flashing or electrical validation'])
(HERE/'build_verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False,indent=2))

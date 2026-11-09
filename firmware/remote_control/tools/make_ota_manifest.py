from pathlib import Path
import hashlib,json,re,zipfile
from prepare import ROOT

def main():
    image=ROOT/"esp32/build/RemoteControl.ino.bin"
    data=image.read_bytes()
    assert data[0]==0xe9 and int.from_bytes(data[12:14],"little")==9
    assert data[32:36]==bytes.fromhex("3254cdab") and b"TREE_ROBOT_S3_RC_OTA_V1" in data
    version=re.search(r'FW_VERSION="([^"]+)"',(ROOT/"esp32/RemoteControl/ota_support.h").read_text()).group(1)
    manifest={"target":"tree-robot-esp32s3","version":version,"size":len(data),"sha256":hashlib.sha256(data).hexdigest(),"file":image.name}
    path=image.with_name("RemoteControl.ota.json")
    path.write_text(json.dumps(manifest,indent=2),encoding="utf-8")
    print("Created OTA manifest:",path,"version",version,"bytes",len(data))
    delivery=ROOT/'delivery';delivery.mkdir(exist_ok=True)
    archive=delivery/('ESP32_S3_OTA_'+version+'.zip')
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        z.write(image,image.name);z.write(path,path.name)
        z.writestr('README.txt','ESP32-S3 OTA application package\nConnect to TreeRobotTest, open http://192.168.4.1/update\nLog in with the device-local OTA credentials. Select BOTH the application BIN and matching JSON manifest.\nThis package contains no OTA password. Do not flash this application BIN at address 0x0.\n')
    print('OTA-only bundle:',archive)
if __name__=="__main__":main()

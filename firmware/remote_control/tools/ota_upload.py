"""Authenticated local-network OTA client. Credentials stay in a local JSON file."""
from pathlib import Path
import argparse,base64,hashlib,json,time,urllib.request,urllib.error,urllib.parse,uuid
from prepare import ROOT

class OTAClient:
    def __init__(self,host,credentials):
        self.host=host.rstrip('/')
        self.opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
        access=json.loads(Path(credentials).read_text(encoding='utf-8'))
        self.authorization='Basic '+base64.b64encode((access['username']+':'+access['password']).encode()).decode()
    def request(self,path,fields=None,*,data=None,headers=None,method=None,authenticated=True,timeout=30):
        h={'Authorization':self.authorization} if authenticated else {}
        if headers:h.update(headers)
        if fields is not None:
            data=urllib.parse.urlencode(fields).encode();h['Content-Type']='application/x-www-form-urlencoded'
        request=urllib.request.Request(self.host+path,data=data,headers=h,method=method)
        with self.opener.open(request,timeout=timeout) as response:return json.loads(response.read())
    def prepare(self,image,manifest,standalone=False):
        return self.request('/api/ota/prepare',{'target':manifest['target'],'size':len(image),'sha256':manifest['sha256'],'standalone':int(standalone)})['token']
    def ready(self):
        for _ in range(25):
            status=self.request('/api/ota/status')
            if status['error']:raise RuntimeError(status['error'])
            if status['ready']:return status
            time.sleep(.2)
        raise RuntimeError('STM32 stop confirmation timed out')
    def upload(self,token,image):
        boundary='RobotOTA'+uuid.uuid4().hex
        data=(f'--{boundary}\r\nContent-Disposition: form-data; name="firmware"; filename="application.bin"\r\nContent-Type: application/octet-stream\r\n\r\n').encode()+image+f'\r\n--{boundary}--\r\n'.encode()
        return self.request('/api/ota/upload',data=data,headers={'X-OTA-Token':token,'Content-Type':'multipart/form-data; boundary='+boundary},timeout=90)

def main():
    p=argparse.ArgumentParser();p.add_argument('--host',default='http://192.168.4.1')
    p.add_argument('--credentials',type=Path,default=ROOT/'esp32/ota_access.json')
    p.add_argument('--image',type=Path);p.add_argument('--manifest',type=Path);p.add_argument('--standalone',action='store_true')
    args=p.parse_args();client=OTAClient(args.host,args.credentials)
    if not args.image:print(json.dumps(client.request('/api/ota/status'),indent=2));return
    image=args.image.read_bytes();manifest=json.loads(args.manifest.read_text()) if args.manifest else {'target':'tree-robot-esp32s3','sha256':hashlib.sha256(image).hexdigest(),'size':len(image)}
    if manifest['size']!=len(image) or manifest['sha256']!=hashlib.sha256(image).hexdigest():raise SystemExit('Firmware/manifest mismatch')
    token=client.prepare(image,manifest,args.standalone);client.ready();print('Stopped and locked; uploading',len(image),'bytes')
    print(json.dumps(client.upload(token,image)))
if __name__=='__main__':main()

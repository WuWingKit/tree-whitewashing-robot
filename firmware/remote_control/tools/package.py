"""Create a local ready-to-open delivery ZIP, with only selected dependencies."""
from pathlib import Path
import hashlib
import json
import zipfile
from prepare import ROOT

def main():
    out=ROOT/"delivery";out.mkdir(exist_ok=True)
    archive=out/"tree_robot_remote_control_20261108.zip"
    files=[]
    for p in ROOT.rglob("*"):
        if not p.is_file():continue
        rel=p.relative_to(ROOT)
        if any(part in {"delivery","__pycache__"} for part in rel.parts):continue
        if p.suffix in {".o",".obj",".axf",".map",".sct",".uvoptx"}:continue
        if "build" in rel.parts and p.suffix not in {".hex",".bin"}:continue
        if "tests" in rel.parts and "build" in rel.parts:continue
        if p.name.startswith(".") and p.name!=".gitignore":continue
        files.append((p,rel.as_posix()))
    manifest={rel:{"bytes":p.stat().st_size,"sha256":hashlib.sha256(p.read_bytes()).hexdigest()} for p,rel in files}
    with zipfile.ZipFile(archive,"w",zipfile.ZIP_DEFLATED) as z:
        for p,rel in files:z.write(p,"tree_robot_remote_control/"+rel)
        z.writestr("tree_robot_remote_control/SHA256_MANIFEST.json",json.dumps(manifest,ensure_ascii=False,indent=2))
    with zipfile.ZipFile(archive) as z:assert z.testzip() is None
    print("Package:",archive)
    print("Files:",len(files),"ZIP bytes:",archive.stat().st_size)
    print("ZIP SHA256:",hashlib.sha256(archive.read_bytes()).hexdigest())

if __name__=="__main__":main()

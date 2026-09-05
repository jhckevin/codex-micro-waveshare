"""Validate the documentation links and record screenshot provenance (run from repo root)."""
from pathlib import Path
import re, json, hashlib, struct, subprocess, sys
root=Path.cwd()
files=[Path("README.md"),Path("docs/USER_GUIDE.zh-CN.md"),Path("docs/render/README.md")]
errors=[]
items=[]
for kind in ("embedded","desktop","diagrams"):
    for p in sorted(Path("docs/images",kind).glob("*.png")):
        data=p.read_bytes()
        if data[:8]!=b"\x89PNG\r\n\x1a\n":
            errors.append("Invalid PNG: "+str(p));continue
        w,h=struct.unpack(">II",data[16:24])
        items.append(dict(path=str(p),kind=kind,width=w,height=h,sha256=hashlib.sha256(data).hexdigest()))
manifest_path=Path("docs/render/manifest.json")
if "--record" in sys.argv:
    commit=subprocess.check_output(["git","rev-parse","HEAD"],text=True).strip()
    manifest_path.write_text(json.dumps(dict(source_commit=commit,note="Documentation-only host renders; simulated values, not hardware validation.",images=items),ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
elif manifest_path.exists():
    old=json.loads(manifest_path.read_text())["images"]
    if old!=items:errors.append("Image manifest is stale; review screenshots then run --record")
for f in files:
    text=f.read_text(encoding="utf-8")
    for t in re.findall(r"!?\[[^\]]*\]\(([^)]+)\)",text):
        if "://" in t or t.startswith("mailto:"):continue
        part,_,anchor=t.partition("#")
        p=f.parent/part if part else f
        if not p.exists():errors.append(str(f)+": missing "+t)
        elif anchor and p.suffix==".md" and 'id="'+anchor+'"' not in p.read_text(encoding="utf-8"):
            errors.append(str(f)+": missing explicit anchor "+t)
    print(str(f),dict(characters=len(text),images=len(re.findall(r"!\[",text))))
print("Unique new captures:",len(items))
print("Errors:",errors)
sys.exit(bool(errors))

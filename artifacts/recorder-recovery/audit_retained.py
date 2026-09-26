#!/usr/bin/env python3
"""Offline verification of retained captures after guest lab removal."""
import hashlib,json,pathlib,tarfile
ROOT=pathlib.Path(__file__).resolve().parents[2]
EVIDENCE=ROOT/'artifacts/recorder-recovery'
archive=ROOT/'artifacts/runtime/recorder-recovery/recorder-captures.tar.gz'
results=[]
with tarfile.open(archive) as t:
 for member in t.getmembers():
  if member.name.endswith('/manifest.json'):
   manifest=json.load(t.extractfile(member));files={}
   base=member.name.rsplit('/',1)[0]
   for name,expected in manifest['files'].items():
    if expected.get('copy_exit')==0:
     data=t.extractfile(base+'/'+name).read()
     digest=hashlib.sha256(data).hexdigest()
     assert len(data)==expected['bytes'] and digest==expected['sha256']
     files[name]={'sha256':digest,'bytes':len(data)}
   results.append({'session':base,'status':manifest['status'],'files':files})
assert len(results)==3
assert sorted(r['status'] for r in results)==['completed','completed','incomplete']
summary=json.loads((EVIDENCE/'guest/trial/SUMMARY.json').read_text())
assert summary['outcome']=='PASS' and summary['inputs_unchanged']
assert summary['cleanup_exit']==0 and not summary['remaining_containers']
for key in ['before_capture','after_capture']:
 assert any(r['files'].get('capture.pcap',{}).get('sha256')==summary[key]['sha256'] for r in results)
result={'outcome':'PASS','archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'sessions':results}
(EVIDENCE/'RETENTION_AUDIT.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))

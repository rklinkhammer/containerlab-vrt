#!/usr/bin/env python3
"""Finite local process checks; never creates VMs or containers."""
import json,os,pathlib,subprocess,tempfile,time,socket
ROOT=pathlib.Path(__file__).resolve().parents[1]
bin=pathlib.Path(os.environ.get('SDR_BIN',str(ROOT/'build/dev')))
def run(malformed=False):
 with tempfile.TemporaryDirectory() as d:
  config=json.loads((ROOT/'generated/detector.json').read_text())
  sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);sock.bind(('127.0.0.1',0));port=sock.getsockname()[1];sock.close();config['spectrum']['port']=port
  path=pathlib.Path(d)/'detector.json';path.write_text(json.dumps(config));out=pathlib.Path(d)/'out'
  with out.open('w') as f:
   p=subprocess.Popen([str(bin/'detector'),'--config',str(path),'--duration-seconds','0.7'],stdout=f,stderr=subprocess.PIPE,env={**os.environ,'VRT_TELEMETRY_INTERVAL_MS':'100'})
   if malformed:
    time.sleep(.15);s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
    for _ in range(4):s.sendto(b'invalid',('127.0.0.1',port));time.sleep(.02)
    s.close()
   _,err=p.communicate(timeout=4)
  assert p.returncode==0,err
  rows=[json.loads(line) for line in out.read_text().splitlines()];t=[r for r in rows if r.get('schema')=='vrt.telemetry/1'];assert len(t)>=5,t
  assert all(len(json.dumps(r).encode())<4096 for r in t)
  assert t[-1]['event']=='shutdown'
  if malformed:assert t[-1]['data']['malformed']==4;assert any(r['state']=='degraded' for r in t)
  else:assert all(r['state']=='idle' for r in t[:-1]);assert t[-1]['data']['datagrams']==0
  return t
first=run();second=run();bad=run(True);assert first[0]['boot_id']!=second[0]['boot_id'];assert first[0]['sequence']==second[0]['sequence']==1
out=ROOT/'artifacts/telemetry';out.mkdir(parents=True,exist_ok=True);(out/'example-events.jsonl').write_text('\n'.join(json.dumps(x) for x in [first[0],bad[-2],bad[-1]])+'\n')
print('idle, malformed, heartbeat continuity, shutdown, restart/reset PASS')

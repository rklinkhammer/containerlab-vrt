#!/usr/bin/env python3
"""Explicit task-owned Linux trial; never creates or selects a VM."""
import json, pathlib, subprocess, time
ROOT=pathlib.Path.home()/'containerlab-vrt'
OUT=ROOT/'artifacts/linux-runtime-qualification'
OUT.mkdir(parents=True, exist_ok=True)
NODES=['radio1','radio2','radio3','radio4','processor','detector','recorder','switch1']
PREFIX='clab-four-radio-sdr-'
def command(args, timeout=60):
 p=subprocess.run(args,cwd=ROOT,capture_output=True,text=True,timeout=timeout)
 if p.returncode: raise RuntimeError(f'{args}: {p.returncode}: {p.stderr[-2000:]}')
 return p.stdout

def snapshot(label):
 result={}
 for node in NODES:
  name=PREFIX+node
  state=json.loads(command(['sudo','docker','inspect',name]))[0]
  result[node]={'id':state['Id'],'state':state['State'],'restart_count':state['RestartCount']}
  if node!='switch1':
   raw=command(['sudo','docker','logs',name]);(OUT/(label+'-'+node+'.jsonl')).write_text(raw)
   records=[json.loads(line) for line in raw.splitlines() if line.startswith('{')]
   telemetry=[x for x in records if x.get('schema')=='vrt.telemetry/1']
   result[node]['telemetry_count']=len(telemetry)
   result[node]['last_telemetry']=telemetry[-1] if telemetry else None
   assert all(len(line.encode())+1<=4096 for line in raw.splitlines() if '"schema":"vrt.telemetry/1"' in line)
   by_boot={}
   for event in telemetry:
    prev=by_boot.get(event['boot_id'],0)
    assert event['sequence']>prev
    by_boot[event['boot_id']]=event['sequence']
   result[node]['boot_ids']=list(by_boot)
 (OUT/(label+'.json')).write_text(json.dumps(result,indent=2)+'\n')
 return result

try:
 with (OUT/'deploy.log').open('w') as log:
  p=subprocess.run(['sudo','bash','scripts/deploy.sh'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,timeout=300)
 assert p.returncode==0,'deployment failed'
 # Give scheduled startup and switch configuration a bounded settling period.
 time.sleep(20)
 start=time.monotonic();snapshot('initial')
 capture=subprocess.Popen(['sudo','bash','scripts/capture.sh'],cwd=ROOT,stdout=(OUT/'capture.log').open('w'),stderr=subprocess.STDOUT)
 while time.monotonic()-start<340:
  time.sleep(max(0,min(30,340-(time.monotonic()-start))))
  print('sustained elapsed',round(time.monotonic()-start),flush=True)
  command(['sudo','docker','ps','--format','{{.Names}} {{.Status}}'])
 assert capture.wait(timeout=5)==0,'bounded recorder capture failed'
 baseline=snapshot('sustained')
 for n in NODES:
  assert baseline[n]['state']['Running'] and not baseline[n]['state']['OOMKilled'] and baseline[n]['restart_count']==0,n
 for n in NODES[:4]:
  event=baseline[n]['last_telemetry'];assert event and event['data']['streaming'] and event['data']['sample_ordinal']>0,n
 for n in ['processor','detector']:
  assert baseline[n]['last_telemetry']['data']['datagrams']>0,n
 # Controlled downstream interruption. This is the task-owned detector only.
 command(['sudo','docker','stop','--time','5',PREFIX+'detector'])
 time.sleep(10)
 snapshot('consumer-stopped')
 command(['sudo','docker','start',PREFIX+'detector'])
 time.sleep(15)
 restarted=snapshot('consumer-restarted')
 assert len(restarted['detector']['boot_ids'])==2,'restart did not establish a new telemetry process identity'
 # Capture control counters while all radios remain available, before producer interruption.
 command(['sudo','docker','stop','--time','5',PREFIX+'processor'])
 snapshot('control-final')
 # Stop all producers; detector should report absent new activity.
 for n in NODES[:4]: command(['sudo','docker','stop','--time','5',PREFIX+n])
 time.sleep(15)
 snapshot('traffic-interrupted')
 for n in ['processor','detector','recorder']:command(['sudo','docker','stop','--time','5',PREFIX+n])
 snapshot('shutdown')
 (OUT/'trial-status.json').write_text(json.dumps({'completed':True,'sustained_seconds':time.monotonic()-start},indent=2)+'\n')
except Exception as error:
 (OUT/'trial-status.json').write_text(json.dumps({'completed':False,'error':str(error)},indent=2)+'\n')
 try:snapshot('failed')
 except Exception:pass
 raise
finally:
 with (OUT/'destroy.log').open('w') as log:
  p=subprocess.run(['sudo','bash','scripts/destroy.sh'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,timeout=120)
 (OUT/'cleanup.json').write_text(json.dumps({'destroy_exit':p.returncode,'remaining_containers':command(['sudo','docker','ps','-a','--format','{{.Names}}'])},indent=2)+'\n')

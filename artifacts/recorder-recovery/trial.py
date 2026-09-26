#!/usr/bin/env python3
"""Explicit recovery trial for a new dedicated VM. Never manages Lima."""
import datetime,json,pathlib,subprocess,time,traceback,hashlib
ROOT=pathlib.Path.home()/'containerlab-vrt'
OUT=ROOT/'artifacts/recorder-recovery/trial';OUT.mkdir(parents=True,exist_ok=True)
CONFIG=json.loads((ROOT/'config/lab.json').read_text())
NODES=[r['id'] for r in CONFIG['radios']]+['processor','detector','recorder',CONFIG['switch']['name']]
PREFIX='clab-'+CONFIG['lab_name']+'-'
TOPO=str(ROOT/'generated/four-radio.clab.yml')
TARGET='recorder'
RESULT={'cases':[], 'target':TARGET}
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def run(args,check=True,timeout=60):
 p=subprocess.run(args,cwd=ROOT,capture_output=True,text=True,timeout=timeout)
 if check and p.returncode:raise RuntimeError(f'{args}: exit={p.returncode}: {p.stderr[-1500:]}')
 return p

def lifecycle(label,args):
 p=run(['sudo','containerlab']+args,check=False,timeout=300)
 (OUT/(label+'.log')).write_text(p.stdout+p.stderr)
 return p.returncode

def snapshot(label,since=None):
 result={'timestamp':now(),'nodes':{}}
 for node in NODES:
  p=run(['sudo','docker','inspect',PREFIX+node],check=False)
  if p.returncode: result['nodes'][node]={'absent':True};continue
  d=json.loads(p.stdout)[0]
  result['nodes'][node]={'id':d['Id'],'started_at':d['State']['StartedAt'],'running':d['State']['Running'],'exit':d['State']['ExitCode'],'oom':d['State']['OOMKilled']}
 p=run(['sudo','docker','exec',PREFIX+'detector','ip','-j','addr','show','dev','eth1'],check=False)
 result['detector_interface']={'exit':p.returncode,'interfaces':json.loads(p.stdout) if p.returncode==0 else [],'message':p.stderr.strip()}
 cmd=['sudo','docker','logs']+(['--since',since] if since else [])+[PREFIX+'detector']
 p=run(cmd,check=False);(OUT/(label+'-detector.jsonl')).write_text(p.stdout)
 records=[json.loads(l) for l in p.stdout.splitlines() if l.startswith('{')]
 events=[r for r in records if r.get('schema')=='vrt.telemetry/1']
 heartbeats=[r for r in events if r.get('event')=='heartbeat']
 result['heartbeat']=heartbeats[-1] if heartbeats else None
 result['boots']=list(dict.fromkeys(r['boot_id'] for r in events))
 detections=[r for r in records if r.get('type')=='detection' and r.get('validity')=='valid']
 result['tones']={str(r['sid']):[x['detected_frequency_hz'] for x in detections if x.get('stream_id')==r['sid']] for r in CONFIG['radios']}
 (OUT/(label+'.json')).write_text(json.dumps(result,indent=2)+'\n')
 return result

def ready(state):
 if not all(n.get('running') and not n.get('oom') for n in state['nodes'].values()):return False
 iface=state['detector_interface']['interfaces']
 if not iface or iface[0]['mtu']!=CONFIG['application_mtu'] or 'UP' not in iface[0]['flags']:return False
 address=CONFIG['detector']['address'].split('/')[0]
 if not any(a['local']==address for a in iface[0].get('addr_info',[])):return False
 h=state['heartbeat']
 if not h or h['data'].get('datagrams',0)==0:return False
 tolerance=CONFIG['radio_defaults']['sample_rate_hz']/CONFIG['spectrum']['fft_size']/2
 return all(state['tones'][str(r['sid'])] and all(abs(v-r['signal_hz'])<=tolerance for v in state['tones'][str(r['sid'])]) for r in CONFIG['radios'])

def verify_traffic(label,since):
 deadline=time.monotonic()+90
 index=0
 while True:
  state=snapshot(label+'-probe-'+str(index),since);index+=1
  if ready(state):break
  if time.monotonic()>=deadline:raise RuntimeError(label+': no qualified detector traffic within 90 seconds')
  time.sleep(5)
 first=state
 fresh_since=now()
 time.sleep(30)
 last=snapshot(label+'-sustained',fresh_since)
 assert ready(last) and last['heartbeat']['data']['datagrams']>first['heartbeat']['data']['datagrams'],label+' not advancing'
 assert first['heartbeat']['boot_id']==last['heartbeat']['boot_id'],label+' unexpected restart'
 return last

def unchanged(before,after):
 return [n for n in NODES if n!=TARGET and (before['nodes'][n].get('id'),before['nodes'][n].get('started_at'))!=(after['nodes'][n].get('id'),after['nodes'][n].get('started_at'))]

def records(node,label):
 p=run(['sudo','docker','logs',PREFIX+node],check=False)
 (OUT/(label+'-'+node+'.jsonl')).write_text(p.stdout)
 return [json.loads(l) for l in p.stdout.splitlines() if l.startswith('{')]

def radio_states(label):
 states={}
 for r in CONFIG['radios']:
  inspection=json.loads(run(['sudo','docker','inspect',PREFIX+r['id']]).stdout)[0]
  host=next(n['IPAddress'] for n in inspection['NetworkSettings']['Networks'].values() if n['IPAddress'])
  states[r['id']]=json.loads(run(['python3','scripts/status_probe.py','--host',host,'--port',str(r['status_port'])]).stdout)
 (OUT/(label+'-radio-states.json')).write_text(json.dumps(states,indent=2)+'\n')
 return states

def hashes():
 return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((ROOT/'generated').glob('*')) if p.is_file()}

def capture(label):
 before=set((ROOT/'artifacts/runtime/captures').glob('capture-*'))
 p=run(['sudo','bash','scripts/capture.sh'],check=False,timeout=100)
 (OUT/(label+'.log')).write_text(p.stdout+p.stderr)
 after=set((ROOT/'artifacts/runtime/captures').glob('capture-*'))
 dirs=after-before;assert len(dirs)==1
 folder=dirs.pop();m=json.loads((folder/'manifest.json').read_text())
 (OUT/(label+'-manifest.json')).write_text(json.dumps(m,indent=2)+'\n')
 assert p.returncode==0 and m['status']=='completed'
 audit=pcap_audit(folder)
 (OUT/(label+'-pcap-audit.json')).write_text(json.dumps(audit,indent=2)+'\n')
 return folder,audit

def pcap_audit(folder):
 import struct
 path=folder/'capture.pcap';data=path.read_bytes()
 assert len(data)>=24
 header=struct.unpack_from('<IHHIIII',data)
 assert header==(0xa1b2c3d4,2,4,0,0,65535,1)
 pos=24;count=0;first=None;last=None
 while pos<len(data):
  assert pos+16<=len(data),'partial record header'
  sec,usec,inc,orig=struct.unpack_from('<IIII',data,pos);pos+=16
  assert usec<1000000 and inc<=65535 and inc<=orig and pos+inc<=len(data),'invalid record'
  first=first or [sec,usec];last=[sec,usec];pos+=inc;count+=1
 m=json.loads((folder/'metrics.json').read_text())
 assert count>0 and count==m['pcap']['frames']
 assert len(data)==m['pcap']['bytes'] and len(data)<=67108864
 assert m['pcap']['io_errors']==0 and m['status']=='stopped'
 return {'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),'records':count,
         'first_timestamp':first,'last_timestamp':last,'metrics':m,'complete_record_boundaries':True}

def recorder_ready(label):
 deadline=time.monotonic()+90
 while True:
  events=records('recorder',label)
  h=[e for e in events if e.get('event')=='heartbeat']
  if h and h[-1]['data'].get('rx_frames',0)>0:break
  if time.monotonic()>deadline:raise RuntimeError('RECORDER_NO_FRAMES_WITHIN_90_SECONDS')
  time.sleep(2)
 first=h[-1];time.sleep(30)
 last=[e for e in records('recorder',label+'-sustained') if e.get('event')=='heartbeat'][-1]
 assert last['boot_id']==first['boot_id'] and last['data']['rx_frames']>first['data']['rx_frames']
 iface=json.loads(run(['sudo','docker','exec',PREFIX+TARGET,'ip','-j','addr','show','dev','eth1']).stdout)[0]
 assert iface['mtu']==CONFIG['application_mtu'] and 'UP' in iface['flags']
 (OUT/(label+'-interface.json')).write_text(json.dumps(iface,indent=2)+'\n')
 return last

try:
 RESULT['input_hashes_before']=hashes()
 p=run(['sudo','bash','scripts/deploy.sh'],check=False,timeout=300);(OUT/'initial-deploy.log').write_text(p.stdout+p.stderr);assert p.returncode==0
 baseline=verify_traffic('baseline',now())
 original=recorder_ready('baseline-recorder')
 folder,first=capture('before-replacement')
 RESULT['before_capture']=first
 # Start another bounded capture, then remove the recorder during it.
 capture_root=ROOT/'artifacts/runtime/captures'
 existing=set(capture_root.glob('capture-*'))
 interrupted_log=open(OUT/'interrupted-capture.log','w')
 child=subprocess.Popen(['sudo','bash','scripts/capture.sh'],cwd=ROOT,stdout=interrupted_log,stderr=subprocess.STDOUT)
 deadline=time.monotonic()+10
 while not set(capture_root.glob('capture-*'))-existing:
  assert time.monotonic()<deadline;time.sleep(.1)
 time.sleep(3)
 RESULT['replacement_started_at']=now()
 code=lifecycle('recorder-destroy',['destroy','--topo',TOPO,'--node-filter',TARGET,'--keep-mgmt-net','--graceful'])
 assert code==0
 RESULT['interrupted_exit']=child.wait(timeout=90);interrupted_log.close()
 interrupted=(set(capture_root.glob('capture-*'))-existing).pop()
 manifest=json.loads((interrupted/'manifest.json').read_text())
 (OUT/'interrupted-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
 assert RESULT['interrupted_exit']!=0 and manifest['status']=='incomplete'
 removed=snapshot('removed');assert removed['nodes'][TARGET].get('absent')
 assert not unchanged(baseline,removed)
 assert pcap_audit(folder)['sha256']==first['sha256']
 assert lifecycle('replacement-deploy',['deploy','--topo',TOPO,'--format','json'])==0
 new=recorder_ready('replacement-recorder')
 RESULT['recorder_frames_recovered_at']=now()
 assert new['boot_id']!=original['boot_id']
 after,second=capture('after-replacement')
 RESULT['after_capture']=second
 assert pcap_audit(folder)['sha256']==first['sha256']
 state=verify_traffic('recovered',now())
 assert not unchanged(baseline,state)
 assert state['nodes'][TARGET]['id']!=baseline['nodes'][TARGET]['id']
 RESULT['non_target_changes']=unchanged(baseline,state)
 RESULT['capture_gap_seconds']=(second['first_timestamp'][0]+second['first_timestamp'][1]/1e6)-(first['last_timestamp'][0]+first['last_timestamp'][1]/1e6)
 RESULT['capture_gap_note']='Unrecorded interval between finite captures; not a packet-loss count or precise recorder outage duration.'
 RESULT['outcome']='PASS'
except Exception as e:
 RESULT['outcome']='FAIL';RESULT['reason']=str(e) or type(e).__name__
 (OUT/'exception.txt').write_text(traceback.format_exc())
finally:
 RESULT['input_hashes_after']=hashes()
 RESULT['inputs_unchanged']=RESULT.get('input_hashes_before')==RESULT['input_hashes_after']
 p=run(['sudo','bash','scripts/destroy.sh'],check=False,timeout=120);(OUT/'cleanup.log').write_text(p.stdout+p.stderr)
 RESULT['cleanup_exit']=p.returncode
 RESULT['remaining_containers']=run(['sudo','docker','ps','-a','--format','{{.Names}}']).stdout.splitlines()
 (OUT/'SUMMARY.json').write_text(json.dumps(RESULT,indent=2)+'\n')
 print(json.dumps(RESULT,indent=2),flush=True)

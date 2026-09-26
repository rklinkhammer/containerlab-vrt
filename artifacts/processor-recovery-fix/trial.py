#!/usr/bin/env python3
"""Explicit recovery trial for a new dedicated VM. Never manages Lima."""
import datetime,json,pathlib,subprocess,time,traceback,hashlib
ROOT=pathlib.Path.home()/'containerlab-vrt'
OUT=ROOT/'artifacts/processor-recovery-fix/trial';OUT.mkdir(parents=True,exist_ok=True)
CONFIG=json.loads((ROOT/'config/lab.json').read_text())
NODES=[r['id'] for r in CONFIG['radios']]+['processor','detector','recorder',CONFIG['switch']['name']]
PREFIX='clab-'+CONFIG['lab_name']+'-'
TOPO=str(ROOT/'generated/four-radio.clab.yml')
TARGET='processor'
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

try:
 RESULT['input_hashes_before']=hashes()
 p=run(['sudo','bash','scripts/deploy.sh'],check=False,timeout=300);(OUT/'initial-deploy.log').write_text(p.stdout+p.stderr);assert p.returncode==0
 baseline=verify_traffic('baseline',now())
 old=radio_states('baseline')
 assert all(x['streaming'] for x in old.values())
 initial_events=records('processor','baseline')
 initial_start=next(e['start_epoch'] for e in initial_events if e.get('state')=='coordinated')
 old_boot=next(e['boot_id'] for e in initial_events if e.get('schema')=='vrt.telemetry/1')
 RESULT['baseline']='PASS'
 code=lifecycle('processor-destroy',['destroy','--topo',TOPO,'--node-filter',TARGET,'--keep-mgmt-net','--graceful'])
 case={'case':'native_processor_replacement','destroy_exit':code}
 try:
  assert code==0
  removed=snapshot('processor-removed');assert removed['nodes'][TARGET].get('absent')
  absent=radio_states('processor-absent')
  case['streaming_while_processor_absent']={k:v['streaming'] for k,v in absent.items()}
  code=lifecycle('replacement-deploy',['deploy','--topo',TOPO,'--format','json']);case['deploy_exit']=code;assert code==0
  replaced=verify_traffic('replaced',now())
  deadline=time.monotonic()+90
  index=0
  while True:
   new=radio_states('controller-probe-'+str(index));index+=1
   current=records('processor','controller-probe-'+str(index))
   if all(x['connection']['active'] for x in new.values()) and any(e.get('state')=='coordinated' for e in current):break
   if time.monotonic()>=deadline:raise RuntimeError('CONTROLLER_RECOVERY_TIMEOUT: no coordinated controller with four active radio sessions within 90 seconds after traffic recovery')
   time.sleep(5)
  new=radio_states('replaced')
  assert replaced['nodes'][TARGET]['id']!=baseline['nodes'][TARGET]['id']
  assert not unchanged(baseline,replaced),'non-target identities changed'
  for r in CONFIG['radios']:
   x=new[r['id']];assert x['boot_id']==old[r['id']]['boot_id']
   assert x['streaming'] and x['connection']['active']
   assert x['sample_ordinal']>0
   for key in ['center_hz','sample_rate_hz','bandwidth_hz']:
    assert x['settings'][key]==CONFIG['radio_defaults'][key]
  iface=json.loads(run(['sudo','docker','exec',PREFIX+TARGET,'ip','-j','addr','show','dev','eth1']).stdout)[0]
  (OUT/'processor-interface.json').write_text(json.dumps(iface,indent=2)+'\n')
  assert iface['mtu']==CONFIG['application_mtu'] and 'UP' in iface['flags']
  assert any(a['local']==CONFIG['processor']['address'].split('/')[0] for a in iface['addr_info'])
  assert replaced['heartbeat']['boot_id']==baseline['heartbeat']['boot_id']
  events=records('processor','replaced')
  assert next(e['boot_id'] for e in events if e.get('schema')=='vrt.telemetry/1')!=old_boot
  starts=[e for e in events if e.get('state')=='coordinated'];assert len(starts)==1
  epoch=starts[0]['start_epoch'];assert (epoch['seconds'],epoch['picoseconds'])>(initial_start['seconds'],initial_start['picoseconds'])
  case.update(outcome='PASS',non_target_changes=unchanged(baseline,replaced),initial_start=initial_start,new_start=epoch)
 except Exception as e:
  case.update(outcome='FAIL',reason=str(e) or type(e).__name__)
  (OUT/'case-exception.txt').write_text(traceback.format_exc())
  records('processor','failure')
 RESULT['cases'].append(case)
 p=run(['sudo','docker','stop','--time','15',PREFIX+'processor'],check=False,timeout=30)
 RESULT['processor_stop_exit']=p.returncode
 metrics=[e for e in records('processor','shutdown') if e.get('type')=='controller_metrics']
 RESULT['controller_metrics']=metrics[-1] if metrics else None
 if metrics:
  m=metrics[-1]
  RESULT['controller_acceptance']='PASS' if m['boot_changes']==0 and m['configurations']==4 and m['starts_submitted']==4 and m['starts_admitted']==4 and m['coordinated'] else 'FAIL'
 else:RESULT['controller_acceptance']='FAIL'
except Exception as error:
 RESULT['fatal_error']=str(error);(OUT/'exception.txt').write_text(traceback.format_exc())
finally:
 RESULT['input_hashes_after']=hashes()
 RESULT['inputs_unchanged']=RESULT.get('input_hashes_before')==RESULT['input_hashes_after']
 p=run(['sudo','bash','scripts/destroy.sh'],check=False,timeout=120);(OUT/'cleanup.log').write_text(p.stdout+p.stderr)
 RESULT['cleanup_exit']=p.returncode
 RESULT['remaining_containers']=run(['sudo','docker','ps','-a','--format','{{.Names}}']).stdout.splitlines()
 (OUT/'SUMMARY.json').write_text(json.dumps(RESULT,indent=2)+'\n')
 print(json.dumps(RESULT,indent=2),flush=True)

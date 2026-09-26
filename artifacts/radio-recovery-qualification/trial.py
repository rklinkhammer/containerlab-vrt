#!/usr/bin/env python3
"""Explicit recovery trial for a new dedicated VM. Never manages Lima."""
import datetime,json,pathlib,subprocess,time,traceback,hashlib
ROOT=pathlib.Path.home()/'containerlab-vrt'
OUT=ROOT/'artifacts/radio-recovery-qualification/trial';OUT.mkdir(parents=True,exist_ok=True)
CONFIG=json.loads((ROOT/'config/lab.json').read_text())
NODES=[r['id'] for r in CONFIG['radios']]+['processor','detector','recorder',CONFIG['switch']['name']]
PREFIX='clab-'+CONFIG['lab_name']+'-'
TOPO=str(ROOT/'generated/four-radio.clab.yml')
TARGET=CONFIG['radios'][0]['id']
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

def radio_state(label):
 events=records(TARGET,label)
 h=[e for e in events if e.get('schema')=='vrt.telemetry/1' and e.get('event')=='heartbeat']
 p=run(['sudo','docker','exec',PREFIX+TARGET,'ip','-j','addr','show','dev','eth1'])
 inspection=json.loads(run(['sudo','docker','inspect',PREFIX+TARGET]).stdout)[0]
 host=next(n['IPAddress'] for n in inspection['NetworkSettings']['Networks'].values() if n['IPAddress'])
 status=json.loads(run(['python3','scripts/status_probe.py','--host',host,'--port',str(CONFIG['radios'][0]['status_port'])]).stdout)
 state={'status':status,'heartbeat':h[-1] if h else None,'first_event':next((e for e in events if e.get('schema')=='vrt.telemetry/1'),None),'interface':json.loads(p.stdout)}
 (OUT/(label+'-radio-state.json')).write_text(json.dumps(state,indent=2)+'\n')
 return state

def hashes():
 return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((ROOT/'generated').glob('*')) if p.is_file()}

try:
 RESULT['input_hashes_before']=hashes()
 p=run(['sudo','bash','scripts/deploy.sh'],check=False,timeout=300);(OUT/'initial-deploy.log').write_text(p.stdout+p.stderr);assert p.returncode==0
 baseline=verify_traffic('baseline',now())
 old=radio_state('baseline')
 assert old['heartbeat']['data']['streaming']
 initial_events=records('processor','baseline')
 initial_start=next(e['start_epoch'] for e in initial_events if e.get('state')=='coordinated')
 RESULT['baseline']='PASS'
 phase=now();code=lifecycle('radio-destroy',['destroy','--topo',TOPO,'--node-filter',TARGET,'--keep-mgmt-net','--graceful'])
 case={'case':'native_radio_replacement','destroy_exit':code}
 try:
  assert code==0
  removed=snapshot('radio-removed');assert removed['nodes'][TARGET].get('absent')
  code=lifecycle('replacement-deploy',['deploy','--topo',TOPO,'--format','json']);case['deploy_exit']=code;assert code==0
  # Require fresh detections after deployment; pre-removal records cannot satisfy recovery.
  recovered_since=now()
  replaced=verify_traffic('replaced',recovered_since)
  new=radio_state('replaced')
  assert replaced['nodes'][TARGET]['id']!=baseline['nodes'][TARGET]['id']
  assert not unchanged(baseline,replaced), 'non-target node identity changed'
  assert new['heartbeat']['boot_id']!=old['heartbeat']['boot_id']
  assert new['status']['boot_id']!=old['status']['boot_id']
  assert new['status']['streaming'] and new['status']['sample_ordinal']>0
  for key in ['center_hz','sample_rate_hz','bandwidth_hz']:
   assert new['status']['settings'][key]==CONFIG['radio_defaults'][key]
  assert new['heartbeat']['data']['streaming'] and new['heartbeat']['data']['sample_ordinal']>0
  iface=new['interface'][0]
  assert iface['mtu']==CONFIG['application_mtu'] and 'UP' in iface['flags']
  assert any(a['local']==CONFIG['radios'][0]['address'].split('/')[0] for a in iface['addr_info'])
  assert replaced['heartbeat']['boot_id']==baseline['heartbeat']['boot_id']
  events=records('processor','replaced')
  restarts=[e for e in events if e.get('state')=='radio_restarted']
  assert len(restarts)==1 and restarts[0]['radio_id']==TARGET, 'expected one target restart event'
  epoch=restarts[0]['start_epoch'];assert (epoch['seconds'],epoch['picoseconds'])>(initial_start['seconds'],initial_start['picoseconds'])
  case.update(outcome='PASS',non_target_changes=unchanged(baseline,replaced),initial_start=initial_start,new_start=epoch)
 except Exception as e:case.update(outcome='FAIL',reason=str(e))
 RESULT['cases'].append(case)
 # Metrics are emitted at graceful processor exit, after identity/traffic verification.
 p=run(['sudo','docker','stop','--time','15',PREFIX+'processor'],check=False,timeout=30)
 RESULT['processor_stop_exit']=p.returncode
 metrics=[e for e in records('processor','shutdown') if e.get('type')=='controller_metrics']
 RESULT['controller_metrics']=metrics[-1] if metrics else None
 if metrics:
  m=metrics[-1]
  RESULT['controller_acceptance']='PASS' if m['boot_changes']==1 and m['configurations']==5 and m['starts_submitted']==5 and m['starts_admitted']==5 and m['stale_starts_replayed']==0 and m['reconnects']>=1 else 'FAIL'
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

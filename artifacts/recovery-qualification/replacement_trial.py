#!/usr/bin/env python3
"""Explicit recovery trial for a new dedicated VM. Never manages Lima."""
import datetime,json,pathlib,subprocess,time,traceback
ROOT=pathlib.Path.home()/'containerlab-vrt'
OUT=ROOT/'artifacts/recovery-qualification/replacement';OUT.mkdir(parents=True,exist_ok=True)
CONFIG=json.loads((ROOT/'config/lab.json').read_text())
NODES=[r['id'] for r in CONFIG['radios']]+['processor','detector','recorder',CONFIG['switch']['name']]
PREFIX='clab-'+CONFIG['lab_name']+'-'
TOPO=str(ROOT/'generated/four-radio.clab.yml')
RESULT={'cases':[]}
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
 time.sleep(30)
 last=snapshot(label+'-sustained',since)
 assert ready(last) and last['heartbeat']['data']['datagrams']>first['heartbeat']['data']['datagrams'],label+' not advancing'
 assert first['heartbeat']['boot_id']==last['heartbeat']['boot_id'],label+' unexpected restart'
 return last

def unchanged(before,after):
 return [n for n in NODES if n!='detector' and (before['nodes'][n].get('id'),before['nodes'][n].get('started_at'))!=(after['nodes'][n].get('id'),after['nodes'][n].get('started_at'))]

try:
 p=run(['sudo','bash','scripts/deploy.sh'],check=False,timeout=300);(OUT/'initial-deploy.log').write_text(p.stdout+p.stderr);assert p.returncode==0
 baseline=verify_traffic('baseline',now())
 RESULT['baseline']='PASS'
 phase=now();code=lifecycle('detector-destroy',['destroy','--topo',TOPO,'--node-filter','detector','--keep-mgmt-net','--graceful'])
 case={'case':'native_detector_replacement','destroy_exit':code}
 try:
  assert code==0
  removed=snapshot('detector-removed');assert removed['nodes']['detector'].get('absent')
  code=lifecycle('replacement-deploy',['deploy','--topo',TOPO,'--format','json']);case['deploy_exit']=code;assert code==0
  replaced=verify_traffic('replaced',phase)
  assert replaced['nodes']['detector']['id']!=baseline['nodes']['detector']['id']
  assert replaced['heartbeat']['boot_id']!=baseline['heartbeat']['boot_id']
  case.update(outcome='PASS',non_target_changes=unchanged(baseline,replaced))
 except Exception as e:case.update(outcome='FAIL',reason=str(e))
 RESULT['cases'].append(case)
except Exception as error:
 RESULT['fatal_error']=str(error);(OUT/'exception.txt').write_text(traceback.format_exc())
finally:
 p=run(['sudo','bash','scripts/destroy.sh'],check=False,timeout=120);(OUT/'cleanup.log').write_text(p.stdout+p.stderr)
 RESULT['cleanup_exit']=p.returncode
 RESULT['remaining_containers']=run(['sudo','docker','ps','-a','--format','{{.Names}}']).stdout.splitlines()
 (OUT/'SUMMARY.json').write_text(json.dumps(RESULT,indent=2)+'\n')
 print(json.dumps(RESULT,indent=2),flush=True)

#!/usr/bin/env python3
"""Run only inside the newly created dedicated guest, against its local image."""
import json,pathlib,subprocess,time,os,tempfile,re
ROOT=pathlib.Path(__file__).resolve().parents[1];IMAGE='containerlab-vrt-app:local';rows=[]
def case(name,args,expected=0):
 command=['sudo','docker','run','--rm','--name','telemetry-'+name+'-'+str(os.getpid()),'--network','none','--log-driver','json-file','--log-opt','max-size=8m','--log-opt','max-file=3','--cap-add','NET_RAW','-e','VRT_TELEMETRY_INTERVAL_MS=100','-v',str(ROOT/'generated')+':/config:ro',IMAGE]+args
 with tempfile.TemporaryDirectory() as scratch:
  cid=pathlib.Path(scratch)/'cid'
  command[command.index('--rm'):command.index('--rm')]=['--cidfile',str(cid)]
  try:
   p=subprocess.run(command,text=True,capture_output=True,timeout=20)
  finally:
   if cid.exists():
    identity=cid.read_text().strip()
    if re.fullmatch('[a-f0-9]{64}',identity):subprocess.run(['sudo','docker','rm','-f',identity],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 assert p.returncode==expected,(name,p.returncode,p.stderr)
 events=[json.loads(l) for l in p.stdout.splitlines() if l.startswith('{')];t=[x for x in events if x.get('schema')=='vrt.telemetry/1'];assert t,(name,p.stdout)
 assert all(len(json.dumps(x).encode())<=4096 for x in t)
 rows.append({'case':name,'outcome':'PASS','events':len(t),'last':t[-1]});return t
# Explicit loopback derivatives; original generated source bytes remain unchanged.
for role,file in [('radio','radio1.json'),('processor','processor.json')]:
 config=json.loads((ROOT/'generated'/file).read_text())
 if role=='radio':config['radio']['address']='127.0.0.1/8';config['iq_destination']['host']='127.0.0.1'
 else:
  config['application']['address']='127.0.0.1/8';config['spectrum']['destination']['host']='127.0.0.1'
  for radio in config['radios']:radio['control']['host']='127.0.0.1';radio['status']['host']='127.0.0.1'
 (ROOT/'generated'/('telemetry-'+role+'.json')).write_text(json.dumps(config))
case('processor-idle',['processor','--config','/config/telemetry-processor.json','--duration-seconds','0.7'])
# timeout provides SIGTERM for radio's supported graceful shutdown;124 is timeout's status.
case('radio-idle',['/usr/bin/timeout','--signal=TERM','--kill-after=3','1','radio','--config','/config/telemetry-radio.json'],124)
case('detector-idle',['detector','--config','/config/detector.json','--duration-seconds','0.7'])
t=case('recorder-empty',['recorder','--interface','lo','--metrics','/tmp/metrics.json','--pcap','/tmp/capture.pcap','--duration-seconds','1']);assert t[-1]['data']['durably_committed_bytes'] is None
# /dev/full intentionally produces a write/flush failure inside this one ephemeral container.
t=case('recorder-full',['recorder','--interface','lo','--metrics','/tmp/metrics.json','--pcap','/dev/full','--duration-seconds','1']);assert t[-1]['data']['pcap_io_errors']>=1
case('recorder-bad-path',['recorder','--interface','lo','--metrics','/proc/forbidden/metrics.json','--pcap','/tmp/capture.pcap','--duration-seconds','1'],2)
(ROOT/'artifacts/telemetry').mkdir(parents=True,exist_ok=True);(ROOT/'artifacts/telemetry/linux-results.json').write_text(json.dumps(rows,indent=2)+'\n')
print(json.dumps(rows))

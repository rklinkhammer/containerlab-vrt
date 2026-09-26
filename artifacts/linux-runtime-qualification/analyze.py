#!/usr/bin/env python3
"""Review retained synthetic trial output; no runtime access."""
import json,pathlib,datetime
ROOT=pathlib.Path(__file__).resolve().parents[2]
OUT=pathlib.Path(__file__).resolve().parent
EVIDENCE=OUT/'guest'
config=json.loads((ROOT/'config/lab.json').read_text())
summary={'roles':{},'checks':{}}
for node in [r['id'] for r in config['radios']]+['processor','detector','recorder']:
 lines=(EVIDENCE/('sustained-'+node+'.jsonl')).read_text().splitlines()
 records=[json.loads(line) for line in lines if line.startswith('{')]
 events=[r for r in records if r.get('schema')=='vrt.telemetry/1']
 heartbeat=[e for e in events if e['event']=='heartbeat']
 assert heartbeat,node
 by_boot={}
 for e in events:
  assert e['sequence']>by_boot.get(e['boot_id'],0)
  by_boot[e['boot_id']]=e['sequence']
  datetime.datetime.fromisoformat(e['timestamp'].replace('Z','+00:00'))
 assert max(len(line.encode())+1 for line in lines if '"schema":"vrt.telemetry/1"' in line)<=4096
 summary['roles'][node]={'heartbeats':len(heartbeat),'events':len(events),'last_heartbeat':heartbeat[-1]}
for radio in config['radios']:
 data=summary['roles'][radio['id']]['last_heartbeat']['data']
 assert data['streaming'] and data['sample_ordinal']>0
 assert data['control_rx_packets']>256
 assert data['tx_failed_submissions']==0
summary['checks']['radios_streaming_and_beyond_registry_threshold']=True
bin_hz=config['radio_defaults']['sample_rate_hz']/config['spectrum']['fft_size']
detections=[json.loads(line) for line in (EVIDENCE/'sustained-detector.jsonl').read_text().splitlines() if line.startswith('{')]
measured={}
for radio in config['radios']:
 values=[x['detected_frequency_hz'] for x in detections if x.get('type')=='detection' and x.get('stream_id')==radio['sid'] and x.get('validity')=='valid']
 assert values and all(abs(v-radio['signal_hz'])<=bin_hz/2 for v in values),radio['id']
 measured[str(radio['sid'])]={'expected_hz':radio['signal_hz'],'observations':len(values),'min_hz':min(values),'max_hz':max(values),'tolerance_hz':bin_hz/2}
summary['checks']['configured_tones']=measured
for role,errors in [('processor',['malformed','packet_gaps','send_failures']),('detector',['malformed','sequence_gaps'])]:
 data=summary['roles'][role]['last_heartbeat']['data']
 assert data['datagrams']>0
 summary['checks'][role+'_errors']={k:data[k] for k in errors}
summary['controller_final']=[json.loads(line) for line in (OUT/'processor-through-shutdown.jsonl').read_text().splitlines() if line.startswith('{') and json.loads(line).get('type')=='controller_metrics']
(OUT/'ANALYSIS.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary['checks'],indent=2))

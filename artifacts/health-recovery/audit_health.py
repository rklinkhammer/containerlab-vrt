"""Offline runtime health acceptance audit. No VM access."""
import json,pathlib
root=pathlib.Path(__file__).resolve().parent
path=root/'guest/trial/shutdown-processor.jsonl'
events=[]
for line in path.read_text().splitlines():
 assert len(line.encode())+1<=4096
 if not line.startswith('{'):continue
 e=json.loads(line)
 if e.get('schema')=='vrt.telemetry/1' and e.get('event') in ('heartbeat','shutdown'):
  assert e['health_policy']=='local-activity/2'
  assert e['data']['health']['error_total']==e['data']['malformed']+e['data']['send_failures']
  events.append(e)
assert events
bad=[e for e in events if e['event']=='heartbeat' and e['state']=='degraded' and e['data']['health']['new_errors']>0]
assert bad,'actual trial did not establish an error-to-recovery case'
first=bad[0]
recovered=[e for e in events if e['sequence']>first['sequence'] and e['event']=='heartbeat' and e['state']=='healthy' and e['data']['health']['new_errors']==0 and e['data']['malformed']>=first['data']['malformed']>0]
assert recovered,'no measured recovery with historical errors retained'
assert recovered[0]['last_successful_activity']>first['timestamp']
final=events[-1]
assert final['event']=='shutdown' and final['state']=='unknown' and final['ready']==False
assert all(e['boot_id']==first['boot_id'] for e in events)
summary={'outcome':'PASS','observations':[{'sequence':e['sequence'],'timestamp':e['timestamp'],'state':e['state'],'ready':e['ready'],'last_successful_activity':e['last_successful_activity'],'health':e['data']['health'],'discard_reasons':e['data']['discard_reasons']} for e in events]}
(root/'HEALTH_AUDIT.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))

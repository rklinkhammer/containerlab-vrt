"""Offline acceptance audit. Reads retained evidence; never accesses a VM."""
import datetime,json,pathlib
root=pathlib.Path(__file__).resolve().parent
trial=root/'guest/trial'
keys={'limits','invalid_envelope','invalid_context','invalid_data','invalid_samples','waiting_context','timestamp_range'}
result={}
for phase in ['baseline','replaced','shutdown']:
 rows=[];max_bytes=0
 for line in (trial/(phase+'-processor.jsonl')).read_text().splitlines():
  max_bytes=max(max_bytes,len(line.encode()))
  if not line.startswith('{'):continue
  event=json.loads(line)
  row=event if event.get('type')=='metrics' else event.get('data',{})
  if 'discard_reasons' not in row:continue
  assert row['discard_schema']=='processor.discards/1'
  assert set(row['discard_reasons'])==keys
  assert all(type(v)==int and v>=0 for v in row['discard_reasons'].values())
  assert sum(row['discard_reasons'].values())==row['discarded']==row['malformed']
  rows.append(row)
 assert rows and max_bytes<=4096
 result[phase]={'checked_records':len(rows),'max_line_bytes':max_bytes,'last':rows[-1]}
 if phase=='replaced':
  probes=list(trial.glob('replaced-probe-*.json'))
  last_probe=max(probes,key=lambda p:int(p.stem.rsplit('-',1)[-1]))
  ready=json.loads(last_probe.read_text())
  since=int(datetime.datetime.fromisoformat(ready['timestamp']).timestamp()*1e9)
  window=[r for r in rows if r['observed_unix_ns']>=since]
  assert len(window)>=20 and window[-1]['datagrams']>window[0]['datagrams']
  delta={k:window[-1]['discard_reasons'][k]-window[0]['discard_reasons'][k] for k in keys}
  result[phase]['sustained_window']={'records':len(window),'seconds':(window[-1]['observed_unix_ns']-window[0]['observed_unix_ns'])/1e9,'discard_deltas':delta,'stable':not any(delta.values())}
(root/'REASON_AUDIT.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))

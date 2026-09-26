"""Read-only post-run identity audit. No VM or runtime access."""
import json,pathlib
root=pathlib.Path(__file__).resolve().parent
results={}
for attempt in ['trial','attempt-2']:
 p=root/'guest'/attempt
 read=lambda name:json.loads((p/name).read_text())
 before=read('baseline-sustained.json');after=read('replaced-sustained.json')
 radios_before=read('baseline-radio-states.json')
 radiofile='replaced-radio-states.json' if attempt=='trial' else 'controller-probe-0-radio-states.json'
 radios_after=read(radiofile)
 events=lambda name:[json.loads(l) for l in (p/name).read_text().splitlines() if l.startswith('{')]
 boot=lambda name:next(e['boot_id'] for e in events(name) if e.get('schema')=='vrt.telemetry/1')
 result={
 'processor_container_changed':before['nodes']['processor']['id']!=after['nodes']['processor']['id'],
 'processor_boot_changed':boot('baseline-processor.jsonl')!=boot('failure-processor.jsonl'),
 'non_target_changes':[n for n in before['nodes'] if n!='processor' and before['nodes'][n]!=after['nodes'][n]],
 'radio_boots_unchanged':all(radios_before[n]['boot_id']==radios_after[n]['boot_id'] for n in radios_before),
 'detector_boot_unchanged':before['heartbeat']['boot_id']==after['heartbeat']['boot_id'],
 'cleanup_exit':read('SUMMARY.json')['cleanup_exit'],
 'remaining_containers':read('SUMMARY.json')['remaining_containers']}
 assert result['processor_container_changed'] and result['processor_boot_changed']
 assert not result['non_target_changes'] and result['radio_boots_unchanged'] and result['detector_boot_unchanged']
 assert result['cleanup_exit']==0 and not result['remaining_containers']
 results[attempt]=result
(root/'identity-audit.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))

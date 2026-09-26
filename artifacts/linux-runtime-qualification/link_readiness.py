#!/usr/bin/env python3
"""Explicit finite Linux image regression for the recorder startup race."""
import subprocess,time,json
rows=[]
for name,setup,expected in [('delayed-up','ip link add qualify0 type dummy; (sleep 1; ip link set qualify0 up) &',0),('never-up','ip link add qualify0 type dummy;',2)]:
 start=time.monotonic()
 p=subprocess.run(['sudo','docker','run','--rm','--name','qualify-recorder-'+name,'--network','none','--cap-add','NET_ADMIN','--cap-add','NET_RAW','containerlab-vrt-app:local','sh','-c',setup+' recorder --interface qualify0 --metrics /tmp/check.json --pcap /tmp/check.pcap --duration-seconds 1'],capture_output=True,text=True,timeout=40)
 elapsed=time.monotonic()-start
 events=[json.loads(line) for line in p.stdout.splitlines() if line.startswith('{')]
 assert p.returncode==expected,(name,p.returncode,p.stderr)
 if name=='delayed-up':
  assert elapsed>=1.8 and events[-1]['event']=='shutdown'
  assert events[-1]['data']['pcap_io_errors']==0
 else:assert elapsed>=30 and events[-1]['event']=='failure'
 rows.append({'case':name,'exit':p.returncode,'elapsed_seconds':elapsed,'events':events})
print(json.dumps(rows,indent=2))

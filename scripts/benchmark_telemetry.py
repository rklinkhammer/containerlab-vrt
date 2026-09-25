#!/usr/bin/env python3
"""Same finite native UDP integration workload; no runtime environment changes."""
import os,subprocess,time,json,statistics,pathlib,resource
r=pathlib.Path(__file__).resolve().parents[1];results=[]
for round in range(3):
 for interval in ([0,5000,100] if round%2==0 else [100,5000,0]):
  before=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.perf_counter()
  p=subprocess.run([str(r/'build/dev/sdr_udp_integration_tests')],env={**os.environ,'VRT_TELEMETRY_INTERVAL_MS':str(interval)},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
  after=resource.getrusage(resource.RUSAGE_CHILDREN);assert p.returncode==0,p.stderr
  results.append({'interval_ms':interval,'wall_s':time.perf_counter()-start,'cpu_s':after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime})
means={str(i):{k:statistics.mean(x[k] for x in results if x['interval_ms']==i) for k in ['wall_s','cpu_s']} for i in [0,5000,100]}
out={'workload':'existing finite loopback UDP integration, identical fixture; Debug macOS build; three interleaved samples per mode','samples':results,'means':means,'wall_change_percent':100*(means['100']['wall_s']/means['0']['wall_s']-1),'cpu_change_percent':100*(means['100']['cpu_s']/means['0']['cpu_s']-1),'limitations':'Timing-driven workload, small samples, not a production throughput benchmark;100ms is50x default heartbeat frequency. Disabled removes new reporter work, not legacy metrics or added counters.'}
(r/'artifacts/telemetry/overhead.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out))

#!/usr/bin/env python3
"""Run inside the Linux test image; same finite workload, interleaved modes."""
import json,os,resource,statistics,subprocess,time
samples=[]
for round in range(5):
 for interval in ([0,5000,100] if round%2==0 else [100,5000,0]):
  before=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.monotonic()
  p=subprocess.run(['/tmp/build/sdr_udp_integration_tests'],env={**os.environ,'VRT_TELEMETRY_INTERVAL_MS':str(interval)},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
  after=resource.getrusage(resource.RUSAGE_CHILDREN)
  assert p.returncode==0,p.stderr
  samples.append({'interval_ms':interval,'wall_s':time.monotonic()-start,'cpu_s':after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime})
means={str(i):{k:statistics.mean(x[k] for x in samples if x['interval_ms']==i) for k in ['wall_s','cpu_s']} for i in [0,5000,100]}
print(json.dumps({'workload':'Linux ARM64 Debug finite loopback UDP integration; five interleaved repetitions; no concurrent qualification load','samples':samples,'means':means,'change_percent':{str(i):{k:100*(means[str(i)][k]/means['0'][k]-1) for k in ['wall_s','cpu_s']} for i in [5000,100]},'limitations':'Short timing-driven workload; noise is not an optimization claim; disabled reporter retains legacy logging and counter instrumentation. Not an end-to-end switched throughput benchmark.'},indent=2))

#!/usr/bin/env python3
"""Bounded diagnostic capture; invoked after capture.sh verifies the lab."""
import datetime
import hashlib
import json
import pathlib
import signal
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
NAME = 'clab-four-radio-sdr-recorder'
def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()
def run(args, timeout=15):
    return subprocess.run(args, capture_output=True, text=True, timeout=timeout)
def interrupt(signum, frame):
    raise KeyboardInterrupt

def main():
    identity = run(['docker', 'inspect', '--format', '{{.Id}}', NAME])
    if identity.returncode:
        raise RuntimeError('recorder identity unavailable')
    cid = identity.stdout.strip()
    parent = ROOT / 'artifacts/runtime/captures'
    parent.mkdir(parents=True, exist_ok=True)
    out = pathlib.Path(tempfile.mkdtemp(prefix='capture-', dir=parent))
    # Match the existing guest evidence-directory access for sudo invocation.
    out.chmod(0o755)
    remote = '/tmp/' + out.name
    manifest = {'schema': 'recorder.capture/1', 'container_id': cid,
                'started_at': now(), 'status': 'incomplete',
                'duration_limit_seconds': 60, 'byte_limit': 67108864,
                'durable_commit': 'not_measured', 'files': {}}
    code = 1
    signal.signal(signal.SIGTERM, interrupt)
    print('capture_directory=' + str(out), flush=True)
    try:
        setup = run(['docker', 'exec', cid, 'mkdir', remote])
        if setup.returncode:
            raise RuntimeError('capture temporary directory unavailable')
        p = run(['docker', 'exec', cid, 'timeout', '--signal=INT', '--kill-after=5s', '65s',
                 'recorder', '--interface', 'eth1', '--metrics', remote + '/metrics.json',
                 '--duration-seconds', '60', '--pcap', remote + '/capture.pcap',
                 '--capture-bytes', '67108864'], timeout=80)
        manifest['capture_exit'] = p.returncode
        (out / 'telemetry.jsonl').write_text(p.stdout)
        (out / 'diagnostic.log').write_text(p.stderr)
        code = p.returncode
    except KeyboardInterrupt:
        manifest['reason'] = 'caller_interrupted'
        code = 130
    except (RuntimeError, subprocess.TimeoutExpired) as e:
        manifest['reason'] = type(e).__name__
    finally:
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        # Exact original container ID: replacement cannot redirect copy/cleanup.
        for filename in ['capture.pcap', 'metrics.json']:
            try:
                p = run(['docker', 'cp', cid + ':' + remote + '/' + filename, str(out / filename)])
                manifest['files'][filename] = {'copy_exit': p.returncode}
                path = out / filename
                if p.returncode == 0:
                    manifest['files'][filename].update(bytes=path.stat().st_size,
                        sha256=hashlib.sha256(path.read_bytes()).hexdigest())
            except subprocess.TimeoutExpired:
                manifest['files'][filename] = {'reason': 'copy_timeout'}
        try:
            metrics = json.loads((out / 'metrics.json').read_text())
            valid = (metrics['status'] == 'stopped' and metrics['pcap']['io_errors'] == 0
                     and (out / 'capture.pcap').stat().st_size == metrics['pcap']['bytes']
                     and metrics['pcap']['bytes'] <= 67108864)
            if code == 0 and valid:
                manifest['status'] = 'completed'
        except (OSError, ValueError, KeyError):
            pass
        manifest['finished_at'] = now()
        (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        # Retain remote partials on failure; automatic command timeout bounds lifetime.
        if manifest['status'] == 'completed':
            run(['docker', 'exec', cid, 'rm', '-f', remote + '/capture.pcap', remote + '/metrics.json'])
            run(['docker', 'exec', cid, 'rmdir', remote])
    print(json.dumps(manifest, indent=2))
    return 0 if manifest['status'] == 'completed' else (code or 1)

if __name__ == '__main__':
    sys.exit(main())

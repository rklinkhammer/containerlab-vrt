#!/usr/bin/env python3
"""Offline capture lifecycle contracts; no Docker/VM access."""
import importlib.util
import json
import pathlib
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('capture',pathlib.Path(__file__).resolve().parents[1]/'scripts/capture_session.py')
capture=importlib.util.module_from_spec(spec);spec.loader.exec_module(capture)

class CaptureContract(unittest.TestCase):
    def scenario(self, failed=False, interrupted=False):
        with tempfile.TemporaryDirectory() as d:
            root=pathlib.Path(d);calls=[]
            def fake(args,timeout=15):
                calls.append(args)
                if args[1]=='inspect':return subprocess.CompletedProcess(args,0,'exact-old-id\n','')
                self.assertIn('exact-old-id',args[2] if args[1]=='exec' else args[2].split(':')[0])
                if 'timeout' in args:
                    if interrupted:raise KeyboardInterrupt
                    return subprocess.CompletedProcess(args,137 if failed else 0,'{}\n','')
                if args[1]=='cp':
                    if failed or interrupted:return subprocess.CompletedProcess(args,1,'','gone')
                    path=pathlib.Path(args[-1])
                    if path.name=='capture.pcap':path.write_bytes(b'x'*24)
                    else:path.write_text(json.dumps({'status':'stopped','pcap':{'bytes':24,'io_errors':0}}))
                return subprocess.CompletedProcess(args,0,'','')
            with patch.object(capture,'ROOT',root),patch.object(capture,'run',fake),patch.object(capture.signal,'signal'),patch('builtins.print'):
                result=capture.main()
                first=next((root/'artifacts/runtime/captures').iterdir())
                manifest=json.loads((first/'manifest.json').read_text())
                self.assertEqual(manifest['status'],'incomplete' if failed or interrupted else 'completed')
                self.assertEqual(result==0,not (failed or interrupted))
                original=(first/'manifest.json').read_bytes()
                capture.main()
                self.assertEqual((first/'manifest.json').read_bytes(),original)
                self.assertEqual(len(list((root/'artifacts/runtime/captures').iterdir())),2)
                self.assertEqual(any('rm' in c for c in calls),not (failed or interrupted))
    def test_complete_and_distinct_sessions(self):self.scenario()
    def test_container_removed(self):self.scenario(failed=True)
    def test_caller_interrupted(self):self.scenario(interrupted=True)

if __name__=='__main__':unittest.main()

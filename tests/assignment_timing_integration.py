#!/usr/bin/env python3
"""Real C++/Python protocol test with synthetic observations, NOT network results."""
import argparse
import csv
import json
from pathlib import Path
import socketserver
import subprocess
import sys
import tempfile
import threading
import time
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'scripts'), str(ROOT/'rl')]
import run_marl as marl
from centralized_agent import make_agent
from centralized_server import CentralizedDqnService
from centralized_protocol import STATE_DIM, ACTION_DIM
from summarize_assignment_timing import summarize, METHODS


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--lib-dir', type=Path, required=True)
    args = p.parse_args(); lib = args.lib_dir.resolve()
    marl.torch.set_num_threads(1)
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp); binary = root/'fixture'
        subprocess.run(['g++', '-std=c++20', '-O2', '-pthread', '-I'+str(ROOT/'build/include'),
            '-I'+str(ROOT/'contrib/kameda/model/server'), str(ROOT/'tests/assignment_timing_fixture.cc'),
            '-L'+str(lib), '-Wl,-rpath,'+str(lib), '-lns3.44-kameda-optimized',
            '-lns3.44-core-optimized', '-o', str(binary)], check=True)
        dqn = CentralizedDqnService.__new__(CentralizedDqnService)
        dqn.schema_version = 'v1'; dqn.normalization_enabled = False
        dqn.args = SimpleNamespace(eval_only=True)
        dqn.agent = make_agent(STATE_DIM, ACTION_DIM, 7, 32, 1e-3, .99, 'cpu', 'mlp_v1', 16)
        dqn.last_cycle = None; dqn.steps = 0
        directory = root/'python'; directory.mkdir()
        session = marl.Session(marl.Learner(seed=7), 80, 7, 0, 'test', directory)
        calls = [0]
        class Handler(socketserver.StreamRequestHandler):
            def handle(self):
                msg = json.loads(self.rfile.readline())
                time.sleep(.25)  # Deliberate communication/service wait, excluded.
                calls[0] += 1
                if msg.get('schema') == 'marl_v1': reply = session.handle(msg)
                else: reply = json.dumps(dqn.handle(msg))+'\n'
                self.wfile.write(reply.encode())
        with socketserver.TCPServer(('127.0.0.1', 0), Handler) as server:
            thread = threading.Thread(target=server.serve_forever); thread.start()
            try:
                for method in METHODS:
                    out = root/method; out.mkdir()
                    with (out/'console.log').open('w') as log:
                        subprocess.run([str(binary), method, str(out), str(server.server_address[1])],
                            stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
            finally:
                server.shutdown(); thread.join()
        rows = summarize(sorted(root.rglob('assignment_timing_*.csv')))
        assert calls[0] > 5, 'DQN must actually invoke the remote service'
        for row in rows:
            assert row['max_ms'] < 200, 'Injected 250ms wait/callback leaked into compute'
        assert session.ended and len(session.transitions) == 4
        print('PASS: four methods, four decisions each; terminal excluded; injected wait/callback excluded.')
        print('Synthetic fixture only; these times are not experiment results.')


if __name__ == '__main__': main()

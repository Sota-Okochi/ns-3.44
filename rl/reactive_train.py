#!/usr/bin/env python3
"""Single-UE reactive DQN training/evaluation with real next-cycle feedback."""
import argparse
from collections import deque
import copy
import csv
import hashlib
import json
import math
from pathlib import Path
import random
import socketserver
import subprocess
import sys
import threading
import time

import torch
from torch import nn

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'scripts'))
from run_capacity_scenario import generate_events, FIELDS, resolve

SCHEMA = 'reactive_v1_log'


def network(n):
    return nn.Sequential(nn.Linear(15*n+30, 256), nn.ReLU(), nn.Linear(256,256), nn.ReLU(), nn.Linear(256,3*n+1))


class Learner:
    def __init__(self, args, n):
        self.args, self.n = args, n
        torch.set_num_threads(1)
        torch.manual_seed(args.agent_seed)
        self.rng = random.Random(args.agent_seed)
        self.q = network(n)
        self.target = copy.deepcopy(self.q)
        self.optimizer = torch.optim.Adam(self.q.parameters(), lr=args.lr)
        self.replay = deque(maxlen=100000)
        self.steps = self.updates = 0
        if args.checkpoint:
            ck = torch.load(args.checkpoint, map_location='cpu', weights_only=False)
            if ck['schema'] != SCHEMA or ck['n'] != n:
                raise ValueError('Checkpoint schema/N mismatch')
            self.q.load_state_dict(ck['q'])
            self.target.load_state_dict(ck['target'])
            if not args.eval_only:
                raise ValueError('Checkpoint loading currently supported for evaluation only; start training in a new directory')
        self.reset()

    def reset(self):
        self.pending = None
        self.cycle = 0
        self.ended = False
        self.transitions = []
        self.error = None

    def handle(self, msg):
        if msg['schema'] != SCHEMA or msg['cycle'] != self.cycle+1 or self.ended:
            raise ValueError('Invalid schema/cycle order')
        state, valid, h = msg['state'], msg['valid'], msg['h']
        if len(state) != 15*self.n+30 or not all(math.isfinite(x) for x in state) or not math.isfinite(h) or h < 0:
            raise ValueError('Invalid state')
        assignment = msg['assignment']
        if len(assignment) != self.n or any(x not in [0,1,2] for x in assignment):
            raise ValueError('Invalid assignment')
        expected_valid = [0] if msg['done'] else [0]+[1+i*3+b for i,a in enumerate(assignment) for b in range(3) if a != b]
        if valid != expected_valid:
            raise ValueError('Invalid action mask')
        self.cycle += 1
        if self.pending is not None:
            old, action, previous = self.pending
            expected = previous.copy()
            if action:
                expected[(action-1)//3] = (action-1)%3
            if assignment != expected:
                raise ValueError('Requested assignment was not applied')
            reward = h-self.args.switch_penalty*bool(action)/self.n
            transition = (old, action, reward, state, msg['done'], valid)
            self.transitions.append(transition)
            self.log.write(json.dumps(dict(cycle=self.cycle-1, next_cycle=self.cycle, state=old,
                action=action, reward=reward, h_next=h, switched=int(bool(action)), next_state=state,
                done=msg['done'], next_valid=valid, applied_assignment=assignment))+'\n')
            self.log.flush()
        self.pending = None
        if msg['done']:
            self.ended = True
            return dict(action_id=0)
        epsilon = 0 if self.args.eval_only else max(.05, 1-.95*self.steps/self.args.epsilon_steps)
        if self.rng.random() < epsilon:
            action = self.rng.choice(valid)
        else:
            with torch.no_grad():
                values = self.q(torch.tensor(state,dtype=torch.float32))
                action = max(valid,key=lambda a: (float(values[a]),-a))
        if not self.args.eval_only:
            self.steps += 1
        self.pending = (state, action, assignment)
        return dict(action_id=action)

    def finish_episode(self):
        # Commit only successful episodes; failed ns-3 runs never become training data.
        if not self.ended or self.pending is not None:
            raise ValueError('Missing terminal observation')
        losses = []
        if self.args.eval_only:
            return losses
        for tr in self.transitions:
            self.replay.append(tr)
            if len(self.replay) < max(self.args.learning_starts,self.args.batch_size):
                continue
            batch = self.rng.sample(list(self.replay), self.args.batch_size)
            s = torch.tensor([x[0] for x in batch],dtype=torch.float32)
            a = torch.tensor([x[1] for x in batch]).unsqueeze(1)
            r = torch.tensor([x[2] for x in batch])
            ns = torch.tensor([x[3] for x in batch],dtype=torch.float32)
            done = torch.tensor([x[4] for x in batch],dtype=torch.bool)
            mask = torch.zeros((len(batch),3*self.n+1),dtype=torch.bool)
            for i,x in enumerate(batch): mask[i,x[5]] = True
            with torch.no_grad():
                next_q = self.target(ns).masked_fill(~mask,-torch.inf).max(1).values
                target = r+self.args.gamma*(~done)*next_q
            loss = nn.functional.smooth_l1_loss(self.q(s).gather(1,a).squeeze(1),target)
            self.optimizer.zero_grad(); loss.backward()
            nn.utils.clip_grad_norm_(self.q.parameters(),10)
            self.optimizer.step(); self.updates += 1
            if self.updates % 500 == 0: self.target.load_state_dict(self.q.state_dict())
            losses.append(float(loss.detach()))
        return losses

    def save(self,path):
        torch.save(dict(schema=SCHEMA,n=self.n,q=self.q.state_dict(),target=self.target.state_dict(),
                        optimizer=self.optimizer.state_dict(),steps=self.steps,updates=self.updates,
                        args=vars(self.args),torch_rng=torch.get_rng_state(),python_rng=self.rng.getstate()),path)


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        try:
            message=json.loads(self.rfile.readline(2_000_000))
            reply=self.server.learner.handle(message)
        except Exception as e:
            self.server.learner.error=str(e)
            reply=dict(type='error',message=str(e))
        self.wfile.write((json.dumps(reply)+'\n').encode())


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--scenario',default='configs/capacity/fixed_all.json')
    p.add_argument('--episodes',type=int,default=100)
    p.add_argument('--seed-start',type=int,default=1001)
    p.add_argument('--fixed-seed',action='store_true',help='Reuse seed-start for every episode (scenario-specific training)')
    p.add_argument('--agent-seed',type=int,default=1)
    p.add_argument('--output',required=True)
    p.add_argument('--learning-starts',type=int,default=1000)
    p.add_argument('--batch-size',type=int,default=64)
    p.add_argument('--epsilon-steps',type=int,default=10000)
    p.add_argument('--lr',type=float,default=1e-4)
    p.add_argument('--gamma',type=float,default=.99)
    p.add_argument('--switch-penalty',type=float,default=1.)
    p.add_argument('--checkpoint')
    p.add_argument('--eval-only',action='store_true')
    args=p.parse_args()
    if args.episodes<1 or args.batch_size<1 or args.learning_starts<0 or args.epsilon_steps<1 or not 0<=args.gamma<=1 or args.lr<=0 or args.switch_penalty<0:
        p.error('Invalid training parameters')
    if args.eval_only and not args.checkpoint: p.error('evaluation requires --checkpoint')
    config=json.loads(resolve(args.scenario).read_text())
    setting=json.loads(resolve(config['setting_path']).read_text())
    if setting['numCycles']<2 or setting['baseStations']!=3: p.error('Need 3 APs, >=2 cycles')
    output=resolve(args.output)
    output.mkdir(parents=True,exist_ok=False)
    manifest=dict(args=vars(args),scenario=config,setting=setting,torch_version=torch.__version__,
                  schema=SCHEMA,update_timing='episode_commit',qoe_schema='legacy_v1',normalization='log1p_fixed')
    manifest['source_sha256']={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in
        [Path(__file__).resolve(),ROOT/'contrib/kameda/model/server/reactive-dqn.cc',ROOT/'contrib/kameda/model/server/APselection.cc',ROOT/'master/capacity-schedule.cc']}
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2))
    learner=Learner(args,setting['terminals'])
    with socketserver.TCPServer(('127.0.0.1',0),Handler) as server:
        server.learner=learner
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        try:
            for episode in range(args.episodes):
                directory=output/f'episode_{episode:05d}';directory.mkdir()
                cfg=dict(setting,rngSeed=args.seed_start+(0 if args.fixed_seed else episode))
                (directory/'setting.json').write_text(json.dumps(cfg,indent=2))
                variation=dict(config['capacity_variation'])
                if variation['mode']=='random': variation['event_seed']+=episode
                events=generate_events(variation,cfg['numCycles'])
                with (directory/'planned_events.csv').open('w',newline='') as f:
                    w=csv.DictWriter(f,fieldnames=FIELDS,lineterminator='\n');w.writeheader();w.writerows(events)
                command=[str(ROOT/'ns3'),'run','master','--no-build','--',f'--settingPath={directory/"setting.json"}',
                         '--method=reactive_dqn','--maxSwitches=1','--centralizedDqnBootstrapCycles=0',
                         f'--rngSeed={cfg["rngSeed"]}','--mob=1','--pgwCerRate=80Mbps',
                         f'--capacityEventsPath={directory/"planned_events.csv"}',
                         f'--drlServerPort={server.server_address[1]}','--drlTimeoutMs=30000',
                         f'--outputRoot={directory/"simulation"}']
                (directory/'command.json').write_text(json.dumps(command,indent=2))
                learner.reset()
                start=time.monotonic()
                with (directory/'transitions.jsonl').open('w') as log, (directory/'console.log').open('w') as console:
                    learner.log=log
                    result=subprocess.run(command,cwd=ROOT,stdout=console,stderr=subprocess.STDOUT)
                status=dict(returncode=result.returncode,error=learner.error,seconds=time.monotonic()-start,
                            terminal=learner.ended,transitions=len(learner.transitions))
                (directory/'status.json').write_text(json.dumps(status,indent=2))
                if result.returncode or learner.error or len(learner.transitions)!=cfg['numCycles']-1:
                    raise RuntimeError(f'Episode failed; see {directory}')
                losses=learner.finish_episode()
                (directory/'learning.json').write_text(json.dumps(dict(steps=learner.steps,updates=learner.updates,losses=losses)))
                if not args.eval_only: learner.save(output/'model.pt')
                print(f'episode={episode} transitions={len(learner.transitions)} total_steps={learner.steps} updates={learner.updates}',flush=True)
        finally:
            server.shutdown();thread.join()


if __name__=='__main__':
    main()

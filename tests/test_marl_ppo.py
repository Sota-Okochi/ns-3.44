"""No ns-3 processes: unit/integration tests for the new protocol and PPO."""
import copy
from dataclasses import asdict
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import sys

import torch
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from rl.marl.core import Config, Learner, derive, environment_seed, observation, stack
spec = importlib.util.spec_from_file_location('run_marl', ROOT/'scripts/run_marl.py')
runner = importlib.util.module_from_spec(spec); spec.loader.exec_module(runner)
torch.set_num_threads(1)


def message(cycle=1, n=30):
    return dict(schema='marl_v1', cycle=cycle, done=cycle==5, seed=7, sim_time=float(cycle),
                users=[dict(ue_id=i, app=i%4, bs=i%3, tp_mbps=4., tp_status='ok') for i in range(n)],
                aps=[dict(ap_id=i, status='ok', rtt_ms=50.) for i in range(3)])


class MarlTest(unittest.TestCase):
    def test_failed_training_episode_replay_seeds(self):
        self.assertEqual(environment_seed(1, 9), 734)
        with tempfile.TemporaryDirectory() as temp:
            learner = Learner(seed=1)
            a = runner.Session(learner, 30, 734, 9, 'train', Path(temp))
            b = runner.Session(learner, 30, 734, 9, 'smoke', Path(temp))
            self.assertEqual(a.action_seed, b.action_seed)
            obs, _ = observation(message(n=30), Config())
            aa, _, _ = learner.act(obs, a.generator)
            bb, _, _ = learner.act(obs, b.generator)
            torch.testing.assert_close(aa, bb)

    def test_capacity_csv_ns3_line_format(self):
        # Assert bytes: text-mode reading would hide CRLF via universal newlines.
        header = b'cycle_id,ap_id,rate_bps,direction\n'
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)/'planned_events.csv'
            runner.write_capacity_events(path, [])
            self.assertEqual(path.read_bytes(), header)
            runner.write_capacity_events(path, [dict(cycle_id=1, ap_id=0,
                                                    rate_bps=80000000, direction='both')])
            self.assertEqual(path.read_bytes(), header+b'1,0,80000000,both\n')

    def test_seed_order(self):
        values = [environment_seed(1, i) for i in range(1000)]
        self.assertEqual(set(values), set(range(1,1001)))
        self.assertEqual(values[10], environment_seed(1,10))
        self.assertTrue(1 <= environment_seed(1,2047) <= 1000)
        self.assertNotEqual(derive('action_train',1,2,3), derive('action_test',1,2,3))

    def test_qoe_and_zero(self):
        m = message(n=4)
        m['users'][0]['tp_mbps'] = 0.
        o,r = observation(m, Config())
        self.assertEqual(r['users'][0]['s_raw'],0)
        self.assertEqual(r['users'][0]['s_eff'],1e-6)
        self.assertEqual(r['users'][1]['s_eff'],.5)
        self.assertEqual(r['users'][2]['s_eff'],2)
        self.assertEqual(r['users'][3]['s_eff'],.8)
        self.assertAlmostEqual(r['h'],4/(1e6+2+.5+1.25))
        self.assertEqual(o['ue'][0,8],1)
        self.assertEqual(o['ue'][0,12],0)

    def test_timeout_not_missing(self):
        m = message(n=4)
        m['aps'][0].update(status='timeout',rtt_ms=None)
        o,r = observation(m, Config())
        self.assertEqual(r['users'][3]['s_raw'],None)
        self.assertEqual(r['users'][3]['s_eff'],1e-6)
        self.assertEqual(r['users'][0]['s_eff'],4/3)  # TP app not overridden by RTT timeout.
        self.assertEqual(o['ap'][0,2:4].tolist(),[0,1])
        self.assertEqual(o['ue'][3,10],1)
        m['aps'][0]['status']='report_missing'
        o,r = observation(m,Config(),r['users'])
        self.assertIsNone(r['h']); self.assertEqual(o['ue'][3,10],0)
        self.assertEqual(o['ue'][3,12],0)

    def test_aggregation(self):
        m=message(n=4)
        for i,v in enumerate([8.,4.,0.,None]):
            m['users'][i].update(bs=0,app=0,tp_mbps=v)
        o,r=observation(m,Config())
        self.assertEqual(r['aps'][0]['mean_tp_mbps'],4)
        self.assertEqual(r['aps'][0]['tp_target_count'],4)
        self.assertEqual(r['aps'][0]['tp_observed_count'],3)
        self.assertIsNone(r['aps'][1]['mean_tp_mbps'])
        self.assertIsNone(r['h'])

    def test_shapes_permutation_and_initialization(self):
        a=Learner(seed=1); b=Learner(seed=1)
        for n in (30,40,80,100):
            o,_=observation(message(n=n),Config())
            logits=a.actor(o)
            self.assertEqual(tuple(logits.shape),(n,3))
            self.assertEqual(a.critic(o).ndim,0)
            torch.testing.assert_close(logits,b.actor(o))
            p=torch.randperm(n)
            other=dict(o,ue=o['ue'][p])
            torch.testing.assert_close(a.actor(other),logits[p])
            torch.testing.assert_close(a.critic(other),a.critic(o))
        self.assertFalse(any(p is q for p in a.actor.parameters() for q in a.critic.parameters()))

    def test_session_terminal_ppo_and_checkpoint(self):
        learner=Learner(seed=1)
        episodes=[]
        with tempfile.TemporaryDirectory() as temp:
            for e in range(16):
                directory=Path(temp)/str(e);directory.mkdir()
                session=runner.Session(learner,4,7,e,'train',directory)
                m=message(n=4)
                for cycle in range(1,6):
                    m['cycle']=cycle;m['done']=cycle==5
                    reply=session.handle(copy.deepcopy(m)).split()
                    for user,ap in zip(m['users'],reply[4:]):user['bs']=int(ap)
                self.assertTrue(session.ended)
                self.assertEqual(len(session.transitions),4)
                self.assertEqual(session.transitions[-1]['reward'],session.hs[-1])
                self.assertTrue(session.transitions[-1]['done'])
                episodes.append(session.transitions)
            old=copy.deepcopy(learner.actor.state_dict())
            metrics=learner.update(episodes)
            self.assertEqual(metrics['update'],1)
            self.assertTrue(all(torch.isfinite(torch.tensor(v)) for v in metrics.values()))
            self.assertTrue(any(not torch.equal(old[k],v) for k,v in learner.actor.state_dict().items()))
            path=Path(temp)/'latest.pt';learner.save(path,completed_episodes=16)
            new=Learner(seed=1);ck=new.load(path)
            self.assertEqual(ck['completed_episodes'],16)
            for k,v in learner.actor.state_dict().items():torch.testing.assert_close(v,new.actor.state_dict()[k])
            # Deterministic next update after checkpoint restore.
            learner.update(episodes);new.update(episodes)
            for k,v in learner.actor.state_dict().items():torch.testing.assert_close(v,new.actor.state_dict()[k])

    def test_protocol_rejects(self):
        with tempfile.TemporaryDirectory() as temp:
            s=runner.Session(Learner(),4,7,0,'smoke',Path(temp))
            m=message(n=4);s.handle(m)
            with self.assertRaises(ValueError):s.handle(m)
            m=message(cycle=2,n=4)
            m['users'][0]['bs']=(s.pending['action'][0].item()+1)%3
            with self.assertRaises(ValueError):s.handle(m)

    def test_no_fake_missing_reward(self):
        with tempfile.TemporaryDirectory() as temp:
            s=runner.Session(Learner(),4,7,0,'smoke',Path(temp))
            m=message(n=4);m['users'][0]['tp_mbps']=None
            with self.assertRaisesRegex(ValueError,'Evaluation missing'):s.handle(m)
            self.assertEqual(s.transitions,[])
            raw=json.loads((Path(temp)/'observations.jsonl').read_text())
            self.assertIsNone(raw['evaluated']['h'])


if __name__=='__main__':unittest.main()

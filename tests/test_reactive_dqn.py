import copy
import importlib.util
import io
from pathlib import Path
from types import SimpleNamespace
import unittest
import torch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('reactive_train',ROOT/'rl/reactive_train.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)


class ReactiveTest(unittest.TestCase):
    def learner(self, evaluation=False):
        args=SimpleNamespace(agent_seed=1,lr=1e-4,checkpoint=None,eval_only=evaluation,
            switch_penalty=1.,epsilon_steps=100,batch_size=2,learning_starts=2,gamma=.99)
        x=m.Learner(args,1);x.log=io.StringIO();return x

    def observation(self,cycle,assignment=0,done=False):
        return dict(schema=m.SCHEMA,cycle=cycle,done=done,h=.5,state=[0.]*45,
            assignment=[assignment],valid=[0] if done else [0]+[1+b for b in range(3) if b!=assignment])

    def test_transition_terminal_and_reset(self):
        x=self.learner()
        action=x.handle(self.observation(1))['action_id']
        ap=(action-1)%3 if action else 0
        self.assertEqual(x.handle(self.observation(2,ap,True))['action_id'],0)
        self.assertEqual(len(x.transitions),1)
        self.assertEqual(x.transitions[0][2],.5-float(bool(action)))
        self.assertTrue(x.transitions[0][4])
        x.finish_episode();self.assertEqual(len(x.replay),1)
        x.reset();self.assertIsNone(x.pending);self.assertEqual(x.cycle,0)

    def test_mask_and_cycle_rejected(self):
        x=self.learner();bad=self.observation(1);bad['valid']=[0,1,2,3]
        with self.assertRaises(ValueError):x.handle(bad)
        with self.assertRaises(ValueError):x.handle(self.observation(2))

    def test_eval_no_update(self):
        x=self.learner(True);weights=copy.deepcopy(x.q.state_dict())
        a=x.handle(self.observation(1))['action_id']
        x.handle(self.observation(2,(a-1)%3 if a else 0,True));x.finish_episode()
        self.assertEqual((len(x.replay),x.steps,x.updates),(0,0,0))
        self.assertTrue(all(torch.equal(weights[k],v) for k,v in x.q.state_dict().items()))

    def test_masked_target_and_done(self):
        x=self.learner()
        for p in x.q.parameters():p.data.zero_()
        for p in x.target.parameters():p.data.zero_()
        x.target[-1].bias.data.copy_(torch.tensor([2.,1000.,1000.,1000.]))
        s=[0.]*45
        # Only STOP valid: nonterminal target=.5+.99*2; terminal target=.5.
        x.transitions=[(s,0,.5,s,False,[0]),(s,0,.5,s,True,[0])]
        x.ended=True
        losses=x.finish_episode()
        self.assertEqual(x.updates,1)
        self.assertAlmostEqual(losses[0],((2.48-.5)+.5*.5**2)/2,places=5)

    def test_failed_episode_not_committed(self):
        x=self.learner();x.handle(self.observation(1))
        with self.assertRaises(ValueError):x.finish_episode()
        self.assertEqual(len(x.replay),0)


if __name__=='__main__':unittest.main()

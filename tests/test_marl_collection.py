"""No simulations: failure, drain, cache isolation and end-to-end recovery tests."""
import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch

import torch
from test_marl_ppo import runner, message
from rl.marl.core import Learner
from rl.marl.collection import collect_durable

torch.set_num_threads(1)


def result(learner, args, episode, env, phase, base):
    directory=base/f'{phase}_{episode}_mock';directory.mkdir(parents=True)
    session=runner.Session(learner,args.terminals,env,episode,phase,directory)
    m=message(n=args.terminals);m['seed']=env
    for cycle in range(1,6):
        m['cycle']=cycle;m['done']=cycle==5
        reply=session.handle(m).split()
        for u,a in zip(m['users'],reply[4:]):u['bs']=int(a)
    return session.transitions, dict(phase=phase,episode_id=episode,environment_seed=env,
        model_seed=learner.seed,policy_version=learner.updates,
        mean_h_after=sum(session.hs[1:])/4)


class CollectionTest(unittest.TestCase):
    def test_stop_refill_and_drain(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp); ready=threading.Event();release=threading.Event();seen=[]
            # Force task 0 to fail while task 1 is still running.
            def execute(task):
                seen.append(task[0])
                if task[0]==0:
                    ready.wait(3);raise RuntimeError('missing')
                ready.set();release.wait(1)
                tr=[dict(done=i==3,reward=1.,log_probs=torch.zeros(2,3),value=torch.tensor(1.)) for i in range(4)]
                return tr,dict(phase='train')
            with self.assertRaisesRegex(RuntimeError,'preserved'):
                collect_durable([(i,i) for i in range(4)],2,execute,p,dict(phase='train'),lambda *_:None)
            self.assertEqual(set(seen),{0,1})
            self.assertEqual(len(list(p.glob('*.pt'))),1)
            called=[]
            def retry(task):called.append(task[0]);return execute((1,1))
            collect_durable([(0,0),(1,1)],1,retry,p/'new',dict(phase='train'),lambda *_:None,reuse=p)
            self.assertEqual(called,[0])
            called.clear()
            collect_durable([(0,0),(1,1)],1,retry,p/'changed',dict(phase='train',changed=True),lambda *_:None,reuse=p)
            self.assertEqual(called,[0,1])

    def test_recover_initial_and_final_validation(self):
        for fail_version in (0, 1):
            with self.subTest(version=fail_version), tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp);binary=root/'binary';binary.write_text('fake')
                common=['--mode','train','--episodes','16','--terminals','4','--binary',str(binary)]
                failed=root/'failed'; recovered=root/'recovered'
                def failing(l,a,e,s,p,b):
                    if p=='validation' and l.updates==fail_version and e==3:
                        raise RuntimeError('injected validation failure')
                    return result(l,a,e,s,p,b)
                with patch.object(runner,'run_episode',side_effect=failing),patch('builtins.print'):
                    with self.assertRaises(RuntimeError):runner.main(common+['--output',str(failed)])
                calls=[]
                def success(l,a,e,s,p,b):
                    calls.append((p,l.updates,e));return result(l,a,e,s,p,b)
                with patch.object(runner,'run_episode',side_effect=success),patch('builtins.print'):
                    runner.main(common+['--output',str(recovered),'--recover-run',str(failed)])
                replay=[e for p,v,e in calls if p=='validation' and v==fail_version]
                self.assertEqual(replay,list(range(3,8)))
                if fail_version==1:self.assertFalse(any(p=='train' for p,v,e in calls))
                ck=torch.load(recovered/'recovery.pt',weights_only=False)
                self.assertEqual(ck['updates'],1)
                self.assertEqual(ck['pending_phase'],'complete')

    def test_recover_training_identical_to_uninterrupted(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);binary=root/'binary';binary.write_text('fake')
            common=['--mode','train','--episodes','16','--terminals','4','--binary',str(binary)]
            failed=root/'failed';recovered=root/'recovered';reference=root/'reference'
            def failing(l,a,e,s,p,b):
                if p=='train' and e==9:raise RuntimeError('injected missing')
                return result(l,a,e,s,p,b)
            with patch.object(runner,'run_episode',side_effect=failing),patch('builtins.print'):
                with self.assertRaisesRegex(RuntimeError,'preserved'):
                    runner.main(common+['--output',str(failed)])
            ck=torch.load(failed/'recovery.pt',weights_only=False)
            self.assertEqual(ck['updates'],0)
            self.assertEqual(ck['pending_phase'],'train')
            self.assertEqual(len(list((failed/'trajectory_cache').glob('*.pt'))),17) # val8+train9
            calls=[]
            def success(l,a,e,s,p,b):calls.append((p,e));return result(l,a,e,s,p,b)
            with patch.object(runner,'run_episode',side_effect=success),patch('builtins.print'):
                runner.main(common+['--output',str(recovered),'--recover-run',str(failed)])
            self.assertEqual([e for p,e in calls if p=='train'],list(range(9,16)))
            with patch.object(runner,'run_episode',side_effect=result),patch('builtins.print'):
                runner.main(common+['--output',str(reference)])
            a=torch.load(recovered/'latest.pt',weights_only=False)
            b=torch.load(reference/'latest.pt',weights_only=False)
            self.assertEqual(a['updates'],1)
            for net in ['actor','critic']:
                for k in a[net]:torch.testing.assert_close(a[net][k],b[net][k],rtol=0,atol=0)
            # Changing transport settings must reject reuse/checkpoint recovery.
            with patch('builtins.print'):
                with self.assertRaises(SystemExit):
                    runner.main(common+['--output',str(root/'wrong'),'--recover-run',str(failed),
                                        '--report-conn-timeout','0.3'])


if __name__=='__main__':unittest.main()

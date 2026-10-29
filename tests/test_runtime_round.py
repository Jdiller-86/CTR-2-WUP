"""Strict original runtime receipts: incomplete/failed evidence never passes."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('runtime_review',ROOT/'tools/review-runtime-round.py');review=importlib.util.module_from_spec(spec);spec.loader.exec_module(review)

class RuntimeReviewTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        self.manifest={'kind':'ctr2wup-runtime-round','source_commit':'a'*40,'reference':{'arm_instructions':119801,'pcm_sha256':'c'*64}}
        self.rows=[]
        for i,stage in enumerate(['startup','retained_logs_collected','guest_rendered','play_requested','paused','resume_requested','playback_complete','hearing_recorded','resources_closed'],1):
            self.rows.append({'kind':'ctr2wup-guest-dsp','schema':1,'source_commit':'a'*40,'source_dirty':0,'session':'b'*16,'sequence':i,'stage':stage,
                'core_ok':True,'failed':False,'pause':True,'resume':True,'pause_cursor_held':True,'native_complete':True,'ax_owned':False,
                'finite_prerender':True,'installed_exit_tested':False,'dsp_blocks':300,'dsp_samples':48000,'queued':2,'loops':4,
                'output_frames':70398,'arm_instructions':119801,'native_failures':0,'native_releases':1,'native_result':0,
                'cursor':[70398,70398],'pcm_sha256':'c'*64,'hearing':'pass'})
    def run_review(self):
        folder=self.root/'results';folder.mkdir(exist_ok=True)
        (folder/'guest-dsp-test.txt').write_text('\n'.join('DSP_JSON '+json.dumps(r) for r in self.rows)+'\n')
        (folder/'result-guest-dsp-test.txt').write_text('CTR2WUP result report\nDSP_JSON '+json.dumps(self.rows[-1])+'\n')
        return review.review(folder,self.manifest)
    def test_complete_pair_and_zip(self):
        self.assertTrue(self.run_review()['ok']);target=self.root/'ftp.zip'
        with zipfile.ZipFile(target,'w') as z:
            for p in (self.root/'results').iterdir():z.write(p,'dsp-check/results/'+p.name)
        self.assertTrue(review.review(target,self.manifest)['ok'])
    def test_partial_failed_wrong_hearing_and_source(self):
        for field,value in [('failed',True),('source_dirty',1),('core_ok',False),('pause_cursor_held',False),('hearing','fail'),
                            ('pcm_sha256','d'*64),('arm_instructions',True),('output_frames',True),('native_releases',0),('source_commit','d'*40)]:
            with self.subTest(field=field):
                old=copy.deepcopy(self.rows);self.rows[-1][field]=value;self.assertFalse(self.run_review()['ok']);self.rows=old
        self.rows.pop();self.assertFalse(self.run_review()['ok'])
    def test_complete_relisten_and_partial_replay(self):
        replay=copy.deepcopy(self.rows[3:7]);replay[0]['stage']='replay_requested'
        self.rows[7:7]=replay
        for i,row in enumerate(self.rows,1):row['sequence']=i
        self.assertTrue(self.run_review()['ok'])
        self.rows[-4]['stage']='interrupted'
        self.assertFalse(self.run_review()['ok'])
    def test_failed_prior_stage_retained(self):
        self.rows[3]['failed']=True;self.assertFalse(self.run_review()['ok'])
    def test_duplicate_malformed_json(self):
        self.run_review();p=self.root/'results/guest-dsp-test.txt';p.write_text(p.read_text()+'DSP_JSON {"schema":1,"schema":2}\n')
        self.assertFalse(review.review(p.parent,self.manifest)['ok'])
    def test_traversal_rejected(self):
        target=self.root/'bad.zip'
        with zipfile.ZipFile(target,'w') as z:z.writestr('../escape.txt','x')
        with self.assertRaises(ValueError):review.review(target,self.manifest)
    def test_source_generator_current(self):
        spec=importlib.util.spec_from_file_location('dsp_generator',ROOT/'tools/generate-dsp-fixture.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
        for name,data in m.generated().items():self.assertEqual((ROOT/'tests/fixtures/dsp'/name).read_text(),data)

if __name__=='__main__':unittest.main()

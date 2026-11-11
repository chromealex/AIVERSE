import copy
import unittest
from analyze import distribution, summarize, compare

class AnalysisTests(unittest.TestCase):
    def report(self):
        return dict(complete=True, error='', scene={'asset':'a'}, profile='baseline', hardware='gpu', hostId='machine', assetVerification='sha256',
                    renderSettings={'hdr':True}, assetWarnings=[], frames=[dict(camera='a',sample=i,cpuEncodeMs=1.,cpuFrameMs=2.,gpuMs=3.,drawCalls=1) for i in range(30)])
    def test_statistics(self):
        d=distribution(list(range(1,101)))
        self.assertEqual(d['medianMs'],50.5);self.assertAlmostEqual(d['p95Ms'],95.05);self.assertEqual(d['peakMs'],100)
    def test_refuse_incomplete(self):
        r=self.report();r['complete']=False
        with self.assertRaises(ValueError):summarize(r)
    def test_missing_gpu_is_not_zero(self):
        r=self.report();r['frames'][0]['gpuMs']=None
        self.assertIsNone(summarize(r)['a']['gpuMs'])
    def test_comparison(self):
        a=self.report();b=copy.deepcopy(a)
        for r in b['frames']:r['gpuMs']=6
        self.assertEqual(compare(a,b,summarize(a),summarize(b))['a']['gpuMedianRatioBoverA'],2)
        b['scene']={'asset':'b'}
        with self.assertRaises(ValueError):compare(a,b,summarize(a),summarize(b))
    def test_insufficient(self):
        with self.assertRaises(ValueError):distribution([1]*29)

if __name__=='__main__':unittest.main()

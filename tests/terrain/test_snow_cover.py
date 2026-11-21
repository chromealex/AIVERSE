"""Offline snow authoring contracts; native ground is never regenerated here."""
import sys
import unittest
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/terrain'))
from bake_landscape_pbr import smooth,snow_settings,snow_weight

class SnowCover(unittest.TestCase):
 def test_default_cover_is_exact_legacy(self):
  h=np.linspace(-20,400,1024,dtype=np.float32);ny=np.linspace(0,1,1024);v=np.sin(h)
  np.testing.assert_array_equal(snow_weight(h,ny,v,snow_settings({})),smooth(100,155,h+v*8)*smooth(.52,.88,ny))
 def test_high_snowline_preserves_rock_and_excludes_cliffs(self):
  s=snow_settings({'surfaceSnow':dict(lowMetres=220,highMetres=320,minNormalY=.6,fullNormalY=.9,variationMetres=18)})
  self.assertEqual(snow_weight(180,1,0,s),0)
  self.assertEqual(snow_weight(350,.4,0,s),0)
  self.assertEqual(snow_weight(350,1,0,s),1)
  self.assertTrue(0<snow_weight(270,.8,0,s)<1)
 def test_invalid_recipe_rejects_before_bake(self):
  for value in [None,[],{'lowMetres':320,'highMetres':220},{'minNormalY':1},{'variationMetres':-1},{'highMetres':float('inf')},{'lowMetres':True},{'unknown':1}]:
   with self.subTest(value=value),self.assertRaises(ValueError):snow_settings({'surfaceSnow':value})

if __name__=='__main__':unittest.main()

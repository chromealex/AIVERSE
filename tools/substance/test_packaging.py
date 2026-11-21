import unittest
from packaging import rules_for


class PackagingTest(unittest.TestCase):
    def test_semantics_and_orm_deduplication(self):
        prefix = 'assets/substance/hash/'
        rules = rules_for({'texture': prefix + 'baseColor.png',
                           'normalMap': prefix + 'normal.png',
                           'metallicRoughnessMap': prefix + 'orm.png',
                           'occlusionMap': prefix + 'orm.png'})
        self.assertEqual(len(rules), 3)
        self.assertEqual([r['colorSpace'] for r in rules], ['srgb', 'linear', 'linear'])
        self.assertTrue(rules[1]['normalMap'])
        self.assertTrue(all(r['compress'] == 'astc4x4' for r in rules))

    def test_wildcards_and_semantic_conflicts_rejected(self):
        with self.assertRaises(ValueError):
            rules_for({'texture': 'assets/substance/*/base.png'})
        with self.assertRaises(ValueError):
            rules_for({'texture': 'assets/substance/x/map.png',
                       'normalMap': 'assets/substance/x/map.png'})

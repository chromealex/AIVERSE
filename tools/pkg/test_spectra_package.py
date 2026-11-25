import json
import shutil
import tempfile
import unittest
from pathlib import Path
from spectra_package import export_package, plan_package, apply_package, fnv


class ReplayPackageTest(unittest.TestCase):
    def test_leaf_relocation_and_explicit_guid_reuse(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, target = Path(tmp)/"source", Path(tmp)/"target"
            (source/"content/spectrafx/replays").mkdir(parents=True)
            (target/"packages").mkdir(parents=True)
            (target/"content").mkdir()
            data = b"texture"
            meta = b'{"guid":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","settings":{"srgb":true}}'
            (source/"content/fx.png").write_bytes(data)
            (source/"content/fx.png.meta").write_bytes(meta)
            recipe = {"format":"aiverse.spectrafx.replay","version":1,"dependencyVersion":3,
                      "graphSignature":json.dumps({"texture":"content/fx.png"}),
                      "dependencies":[{"path":"content/fx.png","content":fnv(data),"guid":"a"*32,"meta":fnv(meta)}]}
            (source/"content/spectrafx/replays/first.spectra-replay.json").write_text(json.dumps(recipe))
            export_package(source,"first","transfer")
            shutil.copyfile(source/"packages/transfer.aivfx",target/"packages/transfer.aivfx")
            (target/"content/moved.png").write_bytes(data)
            (target/"content/moved.png.meta").write_bytes(meta)
            plan,_=plan_package(target,"transfer")
            self.assertEqual(plan["remapped"],{"content/fx.png":"content/moved.png"})
            self.assertEqual(apply_package(target,"transfer",plan["planRevision"])["installed"],0)
            (target/"content/moved.png.meta").write_bytes(meta.replace(b"a"*32,b"b"*32))
            reuse={"content/fx.png":"b"*32}
            plan,_=plan_package(target,"transfer",reuse)
            self.assertEqual(plan["state"],"ready")
            result=apply_package(target,"transfer",plan["planRevision"],reuse)
            self.assertEqual(result["portable"]["recipe"]["dependencies"][0]["guid"],"b"*32)
            (target/"content/moved.png").write_bytes(b"changed")
            with self.assertRaises(ValueError):
                plan_package(target,"transfer",reuse)

    def test_roundtrip_conflict_and_reuse(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, target = Path(tmp)/"source", Path(tmp)/"target"
            (source/"content/spectrafx/replays").mkdir(parents=True)
            (target/"packages").mkdir(parents=True)
            data = b"texture"
            meta = b'{"guid":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}'
            (source/"content/fx.png").write_bytes(data)
            (source/"content/fx.png.meta").write_bytes(meta)
            recipe = {"format":"aiverse.spectrafx.replay","version":1,"dependencyVersion":3,
                      "projectPath":str(source),"dependencies":[{"path":"content/fx.png",
                      "content":fnv(data),"guid":"a"*32,"meta":fnv(meta)}]}
            (source/"content/spectrafx/replays/first.spectra-replay.json").write_text(json.dumps(recipe))
            export_package(source,"first","transfer")
            shutil.copyfile(source/"packages/transfer.aivfx",target/"packages/transfer.aivfx")
            plan,_ = plan_package(target,"transfer")
            self.assertEqual(plan["state"],"ready")
            self.assertFalse((target/"content").exists())
            with self.assertRaises(ValueError):
                apply_package(target,"transfer","stale")
            result = apply_package(target,"transfer",plan["planRevision"])
            self.assertEqual(result["installed"],2)
            self.assertNotIn("projectPath",result["portable"]["recipe"])
            self.assertEqual((target/"content/fx.png").read_bytes(),data)
            plan,_ = plan_package(target,"transfer")
            self.assertTrue(all(r["state"]=="reuse" for r in plan["files"]))
            (target/"content/fx.png").write_bytes(b"changed")
            self.assertEqual(plan_package(target,"transfer")[0]["state"],"conflict")


if __name__ == "__main__":
    unittest.main()

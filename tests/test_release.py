import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from zipfile import ZipFile


ROOT = Path(__file__).resolve().parents[1]
PAYLOAD = {"hardware.bin", "setup.bin", "E2WPA2Test.prc"}


def run_python(script, *args):
    return subprocess.run([sys.executable, script, *args], capture_output=True, text=True)


class ReleaseToolingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(temporary.cleanup)
        root = Path(temporary.name)
        cls.source = root / "source"
        cls.source.mkdir()
        for name in ("LICENSE", "Makefile", "apply.py"):
            shutil.copyfile(ROOT / name, cls.source / name)
        for name in ("app", "bearssl-0.6", "doc", "driver", "panel", "tests", "tools", "wpa2"):
            shutil.copytree(ROOT / name, cls.source / name,
                            ignore=shutil.ignore_patterns("build", "__pycache__"))

        build = cls.source / "build"
        build.mkdir()
        for name in PAYLOAD | {"nonce_seed.h", "SP3WLAN-Stock.prc"}:
            (build / name).write_bytes(b"synthetic release fixture")
        cls.archive = root / "preview.zip"
        result = run_python(cls.source / "tools/release_bundle.py", cls.archive, cls.source / "bearssl-0.6")
        if result.returncode:
            raise RuntimeError(result.stderr)

        cls.bundle = root / "bundle"
        with ZipFile(cls.archive) as archive:
            archive.extractall(cls.bundle)

    def test_bundle_contains_one_patcher_and_no_stock_prcs(self):
        with ZipFile(self.archive) as archive:
            names = archive.namelist()
            self.assertEqual([name for name in names if name.endswith("apply.py")], ["apply.py"])
            self.assertEqual(archive.read("apply.py"), (ROOT / "apply.py").read_bytes())
            self.assertEqual({name for name in names if name.startswith("payload/")},
                             {f"payload/{name}" for name in PAYLOAD})
            self.assertEqual({name for name in names if name.lower().endswith((".prc", ".pdb"))},
                             {"payload/E2WPA2Test.prc"})

    def test_bad_installer_leaves_no_output(self):
        for root in (self.source, self.bundle):
            with self.subTest(root=root.name), tempfile.TemporaryDirectory() as temporary:
                work = Path(temporary)
                installer = work / "wrong.prc"
                installer.write_bytes(b"this is not the supported installer")
                destination = work / "install"
                result = run_python(root / "apply.py", installer, destination)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("expected the PalmDB WiFi_Installer1.2.prc", result.stderr)
                self.assertEqual(list(work.iterdir()), [installer])

    def test_patcher_preserves_existing_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / "existing"
            destination.mkdir()
            existing = destination / "SP3WLAN-Stock.prc"
            existing.write_bytes(b"previous rollback file")
            result = run_python(self.bundle / "apply.py", Path(temporary) / "unused.prc", destination)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("choose a new output directory", result.stderr)
            self.assertEqual(existing.read_bytes(), b"previous rollback file")

    @unittest.skipUnless(os.environ.get("PALMONE_INSTALLER") or os.environ.get("PALMONE_RELEASE_ZIP"),
                         "set PALMONE_INSTALLER and PALMONE_RELEASE_ZIP for the end-to-end check")
    def test_supported_installer_creates_install_and_rollback_prcs(self):
        self.assertTrue(os.environ.get("PALMONE_INSTALLER"), "set PALMONE_INSTALLER")
        self.assertTrue(os.environ.get("PALMONE_RELEASE_ZIP"), "set PALMONE_RELEASE_ZIP")
        installer = Path(os.environ["PALMONE_INSTALLER"])
        release = Path(os.environ["PALMONE_RELEASE_ZIP"])
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            bundle = work / "bundle"
            destination = work / "install"
            with ZipFile(release) as archive:
                archive.extractall(bundle)
            result = run_python(bundle / "apply.py", installer, destination)
            self.assertEqual(result.returncode, 0, result.stderr)

            types = {
                "E2WPA2Test.prc": b"applE2WT",
                "SP3WLAN-E2-WPA2-Preview.prc": b"librP3wl",
                "WiFiPanel-E2-WPA2-Preview.prc": b"panlWpnl",
                "SP3WLAN-Stock.prc": b"librP3wl",
                "WiFiPanel-Stock.prc": b"panlWpnl",
            }
            self.assertEqual({path.name for path in destination.iterdir()}, set(types))
            for name, identity in types.items():
                self.assertEqual((destination / name).read_bytes()[60:68], identity, name)
            self.assertEqual((destination / "E2WPA2Test.prc").read_bytes(),
                             (bundle / "payload/E2WPA2Test.prc").read_bytes())

            # Hashes of the normalized stock databases from the supported installer.
            stock_hashes = {
                "SP3WLAN-Stock.prc": "58a83f2c74e369fd217ccf75b8c374ecea649c527be27cfd6e9f8761df3764b3",
                "WiFiPanel-Stock.prc": "8ef1dadcbceb130f486164d7ca8f28e7ca829b0fe66b3734706330aeef509eb0",
            }
            for name, digest in stock_hashes.items():
                self.assertEqual(hashlib.sha256((destination / name).read_bytes()).hexdigest(), digest, name)


if __name__ == "__main__":
    unittest.main()

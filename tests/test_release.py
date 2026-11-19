import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from zipfile import ZipFile


ROOT = Path(__file__).resolve().parents[1]
PAYLOAD = {"hardware.bin", "setup.bin", "E2WPA2Test.prc", "E2WPA2InstallerBase.prc"}


def run_python(script, *args):
    return subprocess.run([sys.executable, script, *args], capture_output=True, text=True)


def prc_resources(prc):
    count, = struct.unpack_from(">H", prc, 76)
    entries = [struct.unpack_from(">4sHI", prc, 78 + 10 * i) for i in range(count)]
    entries.sort(key=lambda entry: entry[2])
    ends = [offset for _, _, offset in entries[1:]] + [len(prc)]
    result = {(kind, ident): prc[start:end] for (kind, ident, start), end in zip(entries, ends)}
    if len(result) != count:
        raise ValueError("duplicate PRC resource IDs")
    return result


def decode_recipe(plan):
    header = struct.Struct(">4sHHHH16s")
    kind, ident, stock_size, patched_size, count, digest = header.unpack_from(plan)
    edits_end = header.size + 3 * count
    edits = list(struct.iter_unpack(">HB", plan[header.size:edits_end]))
    suffix = plan[edits_end:]
    if len(edits) != count or patched_size != stock_size + len(suffix):
        raise ValueError("invalid patch resource size")
    return (kind, ident), stock_size, digest, edits, suffix


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
        for name in ("app", "bearssl-0.6", "doc", "driver", "installer", "panel", "tests", "tools", "wpa2"):
            shutil.copytree(ROOT / name, cls.source / name,
                            ignore=shutil.ignore_patterns("build", "__pycache__"))

        build = cls.source / "build"
        (build / "install").mkdir(parents=True)
        for name in (PAYLOAD - {"E2WPA2InstallerBase.prc"}) | {
                "nonce_seed.h", "SP3WLAN-Stock.prc", "E2WPA2Installer.prc", "install/base.prc"}:
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
                             {"payload/E2WPA2Test.prc", "payload/E2WPA2InstallerBase.prc"})

    def test_bad_installer_leaves_no_output(self):
        for root in (self.source, self.bundle):
            for flags in ([], ["--single"]):
                with self.subTest(root=root.name, flags=flags), tempfile.TemporaryDirectory() as temporary:
                    work = Path(temporary)
                    installer = work / "wrong.prc"
                    installer.write_bytes(b"this is not the supported installer")
                    destination = work / "install"
                    result = run_python(root / "apply.py", *flags, installer, destination)
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

    @unittest.skipUnless(os.environ.get("PALMONE_RELEASE_ZIP"), "set PALMONE_RELEASE_ZIP for the archive audit")
    def test_public_archive_has_only_vendor_free_prcs(self):
        with ZipFile(os.environ["PALMONE_RELEASE_ZIP"]) as archive:
            self.assertEqual({n for n in archive.namelist() if n.startswith("payload/")},
                             {f"payload/{name}" for name in PAYLOAD})
            self.assertEqual({n for n in archive.namelist() if n.lower().endswith((".prc", ".pdb"))},
                             {"payload/E2WPA2Test.prc", "payload/E2WPA2InstallerBase.prc"})
            prc = archive.read("payload/E2WPA2InstallerBase.prc")
            self.assertEqual(prc[60:68], b"applE2WI")
            count, = struct.unpack_from(">H", prc, 76)
            types = {struct.unpack_from(">4s", prc, 78 + 10 * i)[0] for i in range(count)}
            self.assertEqual(types, {b"code", b"tAIN", b"tFRM", b"Talt", b"tver", b"tSTR"})

    @unittest.skipUnless(os.environ.get("PALMONE_RELEASE_PATCHER"), "set PALMONE_RELEASE_PATCHER for the PRC audit")
    def test_public_patcher_contains_only_expected_payloads(self):
        prc = Path(os.environ["PALMONE_RELEASE_PATCHER"]).read_bytes()
        self.assertEqual(prc[60:68], b"applE2WI")
        actual = prc_resources(prc)
        with ZipFile(os.environ["PALMONE_RELEASE_ZIP"]) as archive:
            base = prc_resources(archive.read("payload/E2WPA2InstallerBase.prc"))
            status = archive.read("payload/E2WPA2Test.prc")
            self.assertEqual(actual.pop((b"wDat", 1020)), struct.pack(">I", len(status)) + status)
            for slot, key, shim, max_edits in (
                    (1000, (b"amdc", 0), "hardware.bin", 20),
                    (1001, (b"amdi", 0), None, 4),
                    (1010, (b"code", 2), "setup.bin", 6)):
                identity, stock_size, _, edits, appended = decode_recipe(actual.pop((b"wPch", slot)))
                self.assertEqual(identity, key)
                self.assertLessEqual(len(edits), max_edits)
                self.assertEqual(sorted({offset for offset, _ in edits}), [offset for offset, _ in edits])
                self.assertTrue(all(offset < stock_size and mask for offset, mask in edits))
                self.assertEqual(appended, archive.read(f"payload/{shim}") if shim else b"")
        self.assertEqual(actual, base)

    @unittest.skipUnless(os.environ.get("PALMONE_INSTALLER") and os.environ.get("PALMONE_RELEASE_ZIP"),
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

            single = work / "single"
            result = run_python(bundle / "apply.py", "--single", installer, single)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual({path.name for path in single.iterdir()}, {"E2WPA2Installer.prc"})
            prc = (single / "E2WPA2Installer.prc").read_bytes()
            self.assertEqual(prc[60:68], b"applE2WI")
            chunks = {ident: data for (kind, ident), data in prc_resources(prc).items() if kind == b"wDat"}
            for start, name in ((1000, "SP3WLAN-E2-WPA2-Preview.prc"), (1010, "WiFiPanel-E2-WPA2-Preview.prc"),
                                (1020, "E2WPA2Test.prc"), (1030, "SP3WLAN-Stock.prc"), (1040, "WiFiPanel-Stock.prc")):
                payload = b"".join(chunks[ident] for ident in sorted(chunks) if start <= ident < start + 10)
                image = (destination / name).read_bytes()
                self.assertEqual(payload, struct.pack(">I", len(image)) + image, name)

            if patcher := os.environ.get("PALMONE_RELEASE_PATCHER"):
                recipes = prc_resources(Path(patcher).read_bytes())
                for slot, database in ((1000, "SP3WLAN"), (1001, "SP3WLAN"), (1010, "WiFiPanel")):
                    key, stock_size, digest, edits, suffix = decode_recipe(recipes[b"wPch", slot])
                    stock = prc_resources((destination / f"{database}-Stock.prc").read_bytes())[key]
                    patched = prc_resources((destination / f"{database}-E2-WPA2-Preview.prc").read_bytes())[key]
                    self.assertEqual((len(stock), hashlib.md5(stock).digest()), (stock_size, digest), key)
                    reconstructed = bytearray(stock)
                    for offset, mask in edits:
                        reconstructed[offset] ^= mask
                    self.assertEqual(reconstructed + suffix, patched, key)


if __name__ == "__main__":
    unittest.main()

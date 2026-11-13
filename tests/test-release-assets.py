#!/usr/bin/env python3
"""Exercise release assembly without contacting GitHub or publishing a release."""
import json
import os
from pathlib import Path
import runpy
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
prepare = runpy.run_path(str(ROOT / "tools/prepare-flatpak.py"))["prepare"]


class ReleaseAssetsTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="pgadm-release-test-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.environment = dict(os.environ, RELEASE_VERSION="0.2.0", RELEASE_TAG="v0.2.0",
                                GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull)
        self.git("init", "--quiet", "--initial-branch=main")
        self.git("-c", "user.name=Release test", "-c", "user.email=test@example.invalid",
                 "-c", "commit.gpgsign=false", "commit", "--quiet", "--allow-empty", "-m", "fixture")
        self.packages = self.directory / "release"
        self.packages.mkdir()
        for name in ("pgadmin-jr-0.2.0-windows-x64.zip", "pgadmin-jr-0.2.0-windows-x64-setup.exe", "pgadmin-jr_0.2.0-debian12_amd64.deb",
                     "pgadmin-jr_0.2.0-debian13_amd64.deb", "pgadmin-jr_0.2.0-debian12_arm64.deb",
                     "pgadmin-jr_0.2.0-debian13_arm64.deb", "pgadmin-jr-0.2.0-linux-x86_64.flatpak",
                     "pgadmin-jr-0.2.0-macos-arm64.dmg"):
            (self.packages / name).write_bytes(b"test fixture")
        binaries = self.directory / "bin"
        binaries.mkdir()
        fake_gh = binaries / "gh"
        fake_gh.write_text("#!/usr/bin/env python3\nimport json,os,sys\n"
                           "with open(os.environ['RELEASE_TEST_LOG'],'a') as log: log.write(json.dumps(sys.argv[1:])+'\\n')\n"
                           "if sys.argv[1:3] == ['release','view']:\n"
                           " if 'RELEASE_TEST_ASSETS' not in os.environ: sys.exit(1)\n"
                           " sys.stdout.buffer.write(os.environ['RELEASE_TEST_ASSETS'].encode('utf-8'))\n")
        fake_gh.chmod(0o755)
        self.log = self.directory / "gh.json"
        self.environment.update(PATH=str(binaries) + os.pathsep + os.environ["PATH"],
                                RELEASE_TEST_LOG=str(self.log))

    def git(self, *args):
        return subprocess.check_output(["git", *args], cwd=self.directory,
                                       env=self.environment, text=True).strip()

    def publish(self):
        return subprocess.run(["bash", str(ROOT / "tools/publish-release.sh"), str(self.packages)],
                              cwd=self.directory, env=self.environment, capture_output=True, text=True)

    def test_all_eight_packages_are_attached(self):
        self.git("tag", "v0.2.0")
        result = self.publish()
        self.assertEqual(result.returncode, 0, result.stderr)
        arguments = json.loads(self.log.read_text().splitlines()[-1])
        self.assertEqual(arguments[:3], ["release", "create", "v0.2.0"])
        self.assertEqual({Path(p).resolve() for p in arguments[3:11]}, {p.resolve() for p in self.packages.iterdir()})
        self.assertEqual(arguments[arguments.index("--target") + 1], self.git("rev-parse", "HEAD"))

    def test_missing_or_empty_package_blocks_publication(self):
        asset = self.packages / "pgadmin-jr_0.2.0-debian13_amd64.deb"
        for missing in (False, True):
            with self.subTest(missing=missing):
                if missing:
                    asset.unlink()
                else:
                    asset.write_bytes(b"")
                self.assertNotEqual(self.publish().returncode, 0)
                self.assertFalse(self.log.exists())

    def test_tag_without_v_prefix_is_preserved(self):
        self.environment['RELEASE_TAG'] = '0.2.0'
        self.git('tag', '0.2.0')
        result = self.publish()
        self.assertEqual(result.returncode, 0, result.stderr)
        arguments = json.loads(self.log.read_text().splitlines()[-1])
        self.assertEqual(arguments[:3], ['release', 'create', '0.2.0'])

    def test_existing_empty_release_receives_all_packages(self):
        self.environment['RELEASE_TEST_ASSETS'] = ''
        self.git('tag', 'v0.2.0')
        result = self.publish()
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = [json.loads(line) for line in self.log.read_text().splitlines()]
        self.assertEqual(len(calls), 2)
        self.assertEqual(calls[-1][:3], ['release', 'upload', 'v0.2.0'])
        self.assertEqual({Path(p).resolve() for p in calls[-1][3:]}, {p.resolve() for p in self.packages.iterdir()})

    def test_existing_assets_are_preserved(self):
        existing = 'pgadmin-jr-0.2.0-macos-arm64.dmg'
        self.environment['RELEASE_TEST_ASSETS'] = existing
        result = self.publish()
        self.assertEqual(result.returncode, 0, result.stderr)
        upload = json.loads(self.log.read_text().splitlines()[-1])
        self.assertEqual(upload[:3], ['release', 'upload', 'v0.2.0'])
        self.assertEqual({Path(p).name for p in upload[3:]}, {p.name for p in self.packages.iterdir()} - {existing})
        self.assertNotIn('--clobber', upload)

    def test_complete_existing_release_needs_no_upload(self):
        self.environment['RELEASE_TEST_ASSETS'] = '\n'.join(p.name for p in self.packages.iterdir())
        result = self.publish()
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = [json.loads(line) for line in self.log.read_text().splitlines()]
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0][:3], ['release', 'view', 'v0.2.0'])

    def test_missing_windows_installer_blocks_publication(self):
        (self.packages / 'pgadmin-jr-0.2.0-windows-x64-setup.exe').unlink()
        self.assertNotEqual(self.publish().returncode, 0)
        self.assertFalse(self.log.exists())

    def test_missing_arm64_package_blocks_publication(self):
        for debian in (12, 13):
            with self.subTest(debian=debian):
                asset = self.packages / f"pgadmin-jr_0.2.0-debian{debian}_arm64.deb"
                asset.unlink()
                result = self.publish()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(asset.name, result.stderr)
                self.assertFalse(self.log.exists())
                asset.write_bytes(b"test fixture")

    def test_missing_mac_dmg_blocks_publication(self):
        (self.packages / 'pgadmin-jr-0.2.0-macos-arm64.dmg').unlink()
        self.assertNotEqual(self.publish().returncode, 0)
        self.assertFalse(self.log.exists())

    def test_mismatched_tag_blocks_publication(self):
        self.environment["RELEASE_TAG"] = "v0.3.0"
        self.assertNotEqual(self.publish().returncode, 0)
        self.assertFalse(self.log.exists())

    def test_tag_on_different_commit_blocks_publication(self):
        self.git("tag", "v0.2.0")
        self.git("-c", "user.name=Release test", "-c", "user.email=test@example.invalid",
                 "-c", "commit.gpgsign=false", "commit", "--quiet", "--allow-empty", "-m", "other")
        self.assertNotEqual(self.publish().returncode, 0)
        self.assertFalse(self.log.exists())

    def test_flatpak_version_and_source_paths(self):
        output = self.directory / "other" / "manifest.json"
        prepare("0.2.0", output)
        manifest = json.loads(output.read_text())
        application = next(module for module in manifest["modules"] if module["name"] == "pgadmin-jr")
        self.assertIn("-DPGADM_RELEASE_VERSION=0.2.0", application["config-opts"])
        directory_source = next(source for source in application["sources"] if source["type"] == "dir")
        self.assertEqual((output.parent / directory_source["path"]).resolve(), ROOT)
        prepare("", output)
        application = json.loads(output.read_text())["modules"][-1]
        self.assertIn("-DPGADM_RELEASE_VERSION=", application["config-opts"])

    def test_invalid_flatpak_versions_are_rejected(self):
        for value in ("v0.2.0", "01.2.3", "1.2", "1.2.3;echo injected", "../release"):
            with self.subTest(version=value), self.assertRaises(ValueError):
                prepare(value, self.directory / "invalid.json")


if __name__ == "__main__":
    unittest.main()

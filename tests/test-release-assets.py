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
        for name in ("alt-pg-admin-0.2.0-windows-x64.zip", "alt-pg-admin_0.2.0-debian12_amd64.deb",
                     "alt-pg-admin_0.2.0-debian13_amd64.deb", "alt-pg-admin-0.2.0-linux-x86_64.flatpak"):
            (self.packages / name).write_bytes(b"test fixture")
        binaries = self.directory / "bin"
        binaries.mkdir()
        fake_gh = binaries / "gh"
        fake_gh.write_text("#!/usr/bin/env python3\nimport json,os,sys\n"
                           "open(os.environ['RELEASE_TEST_LOG'],'w').write(json.dumps(sys.argv[1:]))\n")
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

    def test_all_four_packages_are_attached(self):
        self.git("tag", "v0.2.0")
        result = self.publish()
        self.assertEqual(result.returncode, 0, result.stderr)
        arguments = json.loads(self.log.read_text())
        self.assertEqual(arguments[:3], ["release", "create", "v0.2.0"])
        self.assertEqual(set(arguments[3:7]), {str(p) for p in self.packages.iterdir()})
        self.assertEqual(arguments[arguments.index("--target") + 1], self.git("rev-parse", "HEAD"))

    def test_missing_or_empty_package_blocks_publication(self):
        asset = self.packages / "alt-pg-admin_0.2.0-debian13_amd64.deb"
        for missing in (False, True):
            with self.subTest(missing=missing):
                if missing:
                    asset.unlink()
                else:
                    asset.write_bytes(b"")
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
        application = next(module for module in manifest["modules"] if module["name"] == "alt-pg-admin")
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

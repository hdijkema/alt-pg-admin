#!/usr/bin/env python3
"""Check Mach-O dependency auditing without requiring macOS commands or signing."""
from pathlib import Path
import runpy
import unittest

ROOT = Path(__file__).resolve().parents[1]
check_dependencies = runpy.run_path(str(ROOT / 'tools/package-macos.py'))['check_dependencies']


class MacPackagingTest(unittest.TestCase):
    def test_plugin_install_name_is_not_an_import(self):
        # macdeployqt can set an absolute ID inside the staged bundle (the reported CI failure).
        check_dependencies(Path('libqcocoa.dylib'), '''Load command 0
          cmd LC_ID_DYLIB
      cmdsize 184
         name /Users/runner/build/image/alt-pg-admin.app/Contents/PlugIns/platforms/libqcocoa.dylib (offset 24)
   time stamp 1 Thu Jan  1 00:00:01 1970
Load command 1
          cmd LC_LOAD_DYLIB
      cmdsize 88
         name @rpath/QtGui.framework/Versions/A/QtGui (offset 24)
Load command 2
          cmd LC_LOAD_DYLIB
      cmdsize 56
         name /usr/lib/libSystem.B.dylib (offset 24)
Load command 3
          cmd LC_LOAD_WEAK_DYLIB
      cmdsize 104
         name /System/Library/Frameworks/AppKit.framework/Versions/C/AppKit (offset 24)
''')

    def test_external_imports_are_rejected(self):
        for command in ('LC_LOAD_DYLIB', 'LC_LOAD_WEAK_DYLIB', 'LC_REEXPORT_DYLIB',
                        'LC_LAZY_LOAD_DYLIB', 'LC_LOAD_UPWARD_DYLIB'):
            for dependency in ('/opt/homebrew/opt/libpq/lib/libpq.5.dylib',
                               '/Users/runner/build Qt/lib/QtCore',
                               '/usr/library/not-a-system-library.dylib'):
                with self.subTest(command=command, dependency=dependency):
                    # No ID: an executable's first library entry must still be checked.
                    commands = f'Load command 0\n cmd {command}\n cmdsize 128\n name {dependency} (offset 24)\n'
                    with self.assertRaisesRegex(ValueError, 'External dependency'):
                        check_dependencies(Path('pgadmin_qt'), commands)

    def test_import_matching_the_install_name_is_still_checked(self):
        commands = '''Load command 0
          cmd LC_ID_DYLIB
      cmdsize 80
         name /opt/homebrew/lib/other.dylib (offset 24)
Load command 1
          cmd LC_LOAD_DYLIB
      cmdsize 80
         name /opt/homebrew/lib/other.dylib (offset 24)
'''
        with self.assertRaisesRegex(ValueError, 'External dependency'):
            check_dependencies(Path('other.dylib'), commands)


if __name__ == '__main__':
    unittest.main()

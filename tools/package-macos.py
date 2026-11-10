#!/usr/bin/env python3
"""Deploy the native app and PostgreSQL client tools, check a mounted DMG, then publish it locally."""
import argparse
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile


def run(arguments, **options):
    """Run a packaging command and fail immediately on an unsuccessful exit."""
    return subprocess.run([str(arg) for arg in arguments], check=True, **options)


def package(build: Path, qt: Path, pg: Path, requested_version: str) -> None:
    """Bundle client dependencies and Qt, ad-hoc sign, mount/test and emit an arm64 DMG."""
    if sys.platform != 'darwin' or os.uname().machine != 'arm64':
        raise ValueError('This initial DMG package requires native Apple Silicon macOS.')
    project = Path(__file__).resolve().parents[1]
    work = Path(tempfile.mkdtemp(prefix='packaging-', dir=build))
    reports = work / 'reports'
    reports.mkdir()
    image_root = work / 'image'
    image_root.mkdir()
    app = image_root / 'alt-pg-admin.app'
    shutil.copytree(build / 'pgadmin_qt.app', app, symlinks=True)
    with (app / 'Contents/Info.plist').open('rb') as stream:
        info = plistlib.load(stream)
    version = info['CFBundleShortVersionString']
    if not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', version):
        raise ValueError('The bundle does not contain a numeric application version.')
    if requested_version and requested_version != version:
        raise ValueError('Bundle version differs from the requested release version.')
    icon = app / 'Contents/Resources' / info['CFBundleIconFile']
    if not icon.is_file():
        raise ValueError('The macOS application icon is missing.')
    binaries = app / 'Contents/MacOS'
    frameworks = app / 'Contents/Frameworks'
    frameworks.mkdir(exist_ok=True)
    tools = ['pg_dump', 'pg_restore', 'psql', 'createdb', 'dropdb']
    dependency_arguments = ['dylibbundler', '-b', '-cd', '-of', '-d', frameworks,
                            '-p', '@executable_path/../Frameworks/']
    for tool in tools:
        shutil.copy2(pg / 'bin' / tool, binaries / tool, follow_symlinks=True)
        (binaries / tool).chmod((binaries / tool).stat().st_mode | 0o200)
        dependency_arguments.extend(['-x', binaries / tool])
    run(dependency_arguments)
    executable = binaries / 'pgadmin_qt'
    links = subprocess.check_output(['otool', '-L', str(executable)], text=True)
    pq_links = [line.strip().split(' (', 1)[0] for line in links.splitlines()[1:] if 'libpq.' in line]
    if len(pq_links) != 1 or not (frameworks / Path(pq_links[0]).name).is_file():
        raise ValueError('The PostgreSQL client library was not deployed beside its tools.')
    run(['install_name_tool', '-change', pq_links[0],
         '@executable_path/../Frameworks/' + Path(pq_links[0]).name, executable])
    deployment = [qt / 'bin/macdeployqt', app, '-always-overwrite', '-no-codesign', '-verbose=2']
    deployment.extend('-executable=' + str(binaries / tool) for tool in tools)
    run(deployment)
    translations = binaries / 'translations'
    translations.mkdir(exist_ok=True)
    for language in ['nl', 'de']:
        shutil.copy2(qt / f'translations/qtbase_{language}.qm', translations / f'qt_{language}.qm')
    licenses = app / 'Contents/Resources/licenses'
    licenses.mkdir(parents=True, exist_ok=True)
    for name in ['LICENSE', 'README.md']:
        shutil.copy2(project / name, licenses / name)
    formulae = ['libpq@18'] + subprocess.check_output(['brew', 'deps', '--formula', 'libpq@18'], text=True).splitlines()
    for formula in formulae:
        prefix = Path(subprocess.check_output(['brew', '--prefix', formula], text=True).strip())
        for source in prefix.iterdir():
            if source.is_file() and re.match(r'(?i)(license|copying|copyright|notice)', source.name):
                destination = licenses / formula
                destination.mkdir(exist_ok=True)
                shutil.copy2(source, destination / source.name)
    # Fail on development-machine paths left by deployment; system libraries stay external.
    linked_binaries = []
    audit = []
    for file in app.rglob('*'):
        if not file.is_file() or file.is_symlink():
            continue
        kind = subprocess.check_output(['file', '-b', str(file)], text=True)
        if 'Mach-O' not in kind:
            continue
        linked_binaries.append(file)
        file.chmod(file.stat().st_mode | 0o200)
        run(['lipo', '-verify_arch', 'arm64', file])
        load_commands = subprocess.check_output(['otool', '-l', str(file)], text=True)
        rpaths = re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset', load_commands)
        for path in dict.fromkeys(rpaths):
            if path.startswith('/') and not path.startswith(('/usr/lib/', '/System/Library/')):
                run(['install_name_tool', '-delete_rpath', path, file])
        if file.parent == binaries and '@executable_path/../Frameworks' not in rpaths:
            run(['install_name_tool', '-add_rpath', '@executable_path/../Frameworks', file])
        output = subprocess.check_output(['otool', '-L', str(file)], text=True)
        audit.append(output)
        for line in output.splitlines()[1:]:
            dependency = line.strip().split(' (', 1)[0]
            if dependency.startswith('/') and not dependency.startswith(('/usr/lib/', '/System/Library/')):
                raise ValueError(f'External dependency left in {file.name}: {dependency}')
    (reports / 'dependencies.txt').write_text('\n'.join(audit))
    for file in linked_binaries:
        run(['codesign', '--force', '--sign', '-', file])
    run(['codesign', '--force', '--deep', '--sign', '-', app])
    run(['codesign', '--verify', '--deep', '--strict', app])
    (image_root / 'Applications').symlink_to('/Applications', target_is_directory=True)
    suffix = '-' + version if requested_version else ''
    image = work / f'alt-pg-admin{suffix}-macos-arm64.dmg'
    run(['hdiutil', 'create', '-volname', 'alt-pg-admin', '-srcfolder', image_root,
         '-format', 'UDZO', image])
    run(['hdiutil', 'verify', image])
    mount = work / 'mounted'
    mount.mkdir()
    run(['hdiutil', 'attach', '-readonly', '-nobrowse', '-mountpoint', mount, image])
    try:
        mounted = mount / app.name
        run(['codesign', '--verify', '--deep', '--strict', mounted])
        environment = dict(os.environ, PATH='/usr/bin:/bin:/usr/sbin:/sbin',
                           QT_PLUGIN_PATH=str(mounted / 'Contents/PlugIns'), QT_QPA_PLATFORM='cocoa')
        for key in ['DYLD_LIBRARY_PATH', 'DYLD_FRAMEWORK_PATH', 'QTDIR', 'QT_QPA_PLATFORM_PLUGIN_PATH']:
            environment.pop(key, None)
        for language in ['nl', 'en', 'de']:
            report = reports / f'startup-{language}.txt'
            run([mounted / 'Contents/MacOS/pgadmin_qt', '--language', language, '--startup-check',
                 '--settings-file', work / f'settings-{language}.ini', '--startup-report', report],
                env=environment, timeout=30)
            if f'Application version: {version}' not in report.read_text():
                raise ValueError('Mounted application startup/version check failed.')
        with (reports / 'postgresql-tools.txt').open('w') as log:
            for tool in tools:
                run([mounted / 'Contents/MacOS' / tool, '--version'], env=environment, stdout=log, timeout=15)
    finally:
        run(['hdiutil', 'detach', mount])
    output = project / 'dist'
    output.mkdir(exist_ok=True)
    shutil.copy2(image, output / image.name)
    print(f'Created and checked {output / image.name}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--qt-root', type=Path, required=True)
    parser.add_argument('--pg-root', type=Path, required=True)
    parser.add_argument('--version', default='')
    args = parser.parse_args()
    package(args.build_dir.resolve(), args.qt_root.resolve(), args.pg_root.resolve(), args.version)

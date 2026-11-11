# GitHub Actions builds

GitHub Actions is the intended build service for distribution. The main workflow is `.github/workflows/build.yml`. It resolves the version once,
then builds Windows x64 and calls reusable workflows for Debian 12/13, Flatpak
and an Apple Silicon macOS DMG. It runs on pushes, pull requests and manual dispatches, with read-only
build permissions. The workflow
belongs at the root of the `alt-pg-admin` Git repository.
The separate release-publishing job has `contents: write`; the build job stays read-only.

## Windows job

The `windows-2022` runner installs Python 3.12 and aqtinstall from upstream commit
`5a49f2b391ae8ee1d3c0dd87f8b33c1d49a4deb7`, downloads
Qt 6.11.1 MSVC 2022 x64 (qtbase, qtsvg and qttools archives) and the existing
PostgreSQL 18.6-5 SDK/test tools, then calls:

```powershell
.\build-release.cmd '-DPGADM_BUILD_TESTS=OFF' '-DPGADM_RELEASE_VERSION=0.2.0'
.\tools\install-inno-setup.ps1
.\tools\package-windows.ps1 -QtRoot $env:PGADM_QT_ROOT -Version 0.2.0 `
  -InnoCompiler ./build/tools/inno-setup/ISCC.exe -TestInstaller
```

`build-release.cmd` discovers Visual Studio using vswhere. `PGADM_QT_ROOT`,
`PGADM_CMAKE` and `PGADM_NINJA` override the existing local defaults. This lets
the local machine and hosted runner use the same build and deployment commands.
No GitHub-specific build logic is added to the C++ application.
The pinned aqt revision supports Qt 6.11's split Windows repository layout;
PyPI release 3.3.0 failed that lookup during local validation. No vendor source
patch is maintained. Replace the revision with a tested release when available.

The distribution job explicitly builds CMake Release with integration test targets
disabled. It does not start PostgreSQL or run the integration suite. The local
integration scripts remain available for development. Packaging retains its
isolated startup smoke check; no production credentials or signing secrets are needed.
Checkout, Python setup and artifact upload use their v6 actions with Node.js 24.

After successful packaging, the job uploads `alt-pg-admin-windows-x64`, containing
the portable ZIP and the Inno Setup installer. The startup and installer reports are uploaded even after a failed packaging step
and retained for 14 days. Database data directories and application/user settings
are excluded. Ordinary branch/PR builds produce Actions artifacts only.

## Versioned releases

There are two explicit ways to publish a release after all platform builds succeed:

- Push a numeric tag such as `0.2.0` or `v0.2.0`. The exact tag spelling is preserved.
- Open Actions > Release build > Run workflow, select the source branch
  and enter `0.2.0` in the version field.
  An optional `v` prefix is accepted. Leaving the field empty builds only.

Use `major.minor.patch` without leading zeroes or prerelease suffixes. Invalid
versions fail before dependency downloads. A manual run on a tag must request
that same tag's version (with or without `v`); it publishes to the selected tag.
A manual branch run creates a `v`-prefixed tag. PRs and ordinary branch pushes never publish a release.

The version becomes `PGADM_RELEASE_VERSION` in CMake, the application/About version,
and each platform package, including the embedded Flatpak application version.
For example: `alt-pg-admin-0.2.0-windows-x64.zip`. The startup smoke
report verifies the executable version before a versioned archive is created.
Without a release override, the current development version is 0.1.0.

The publishing job waits for Windows, Debian 12, Debian 13, Flatpak and macOS to succeed.
It downloads only the package artifacts from that same workflow run, checks that
all eight expected versioned files are present and nonempty, and checks an existing
tag against the built commit. `tools/publish-release.sh` creates a missing release
with generated notes, or uploads missing assets to an existing release. All eight
packages must be available from the same workflow run:

- `alt-pg-admin-0.2.0-windows-x64.zip`
- `alt-pg-admin-0.2.0-windows-x64-setup.exe`
- `alt-pg-admin_0.2.0-debian12_amd64.deb`
- `alt-pg-admin_0.2.0-debian13_amd64.deb`
- `alt-pg-admin_0.2.0-debian12_arm64.deb`
- `alt-pg-admin_0.2.0-debian13_arm64.deb`
- `alt-pg-admin-0.2.0-linux-x86_64.flatpak`
- `alt-pg-admin-0.2.0-macos-arm64.dmg`

Reports are retained as Actions artifacts and are not attached to releases. A manual release creates a missing tag at that commit.
Existing release titles, notes and assets are preserved. This also supports a
release created through the GitHub UI before packaging finishes. Repeating the
workflow adds only missing asset names; a complete release needs no upload.
The main workflow must be present in the tagged commit. To use workflow fixes,
push those fixes first and tag a new version; rerunning an older run uses its
original workflow. Do not rename unversioned development artifacts to repair an
old release: their embedded application version does not match the release.
All action runtimes, including download-artifact v7, use Node.js 24.

The release workflow has been checked locally with actionlint and the resolver's
positive/negative cases (`tests/test-release-version.ps1`). The cross-platform release helpers also pass local tests with a fake GitHub CLI,
including missing/empty packages, mismatched tags, and tags on different commits.
Publication itself has not been performed from this workspace.
On 10 November 2026, a Release build and versioned 0.1.0 ZIP passed the startup
version check and ZIP CRC check. Packaging with a deliberately different version
was rejected. PostgreSQL integration tests were not run for these workflow changes.

## Portable package

`tools/package-windows.ps1` copies an explicit list of executable files, Qt/libpq
DLLs, Qt plugin folders and dependency notices into a new `build/Release/packaging-*`
directory. It adds the release MSVC runtime DLLs from Visual Studio app-locally.
Tests, PostgreSQL server binaries, database files and settings are not packaged.

The staged application runs a native Windows startup smoke check with a reduced
PATH and explicit settings outside the packaged folder. Only after that succeeds
is a ZIP written to `dist/alt-pg-admin-windows-x64.zip`. It extracts to an
`alt-pg-admin` folder containing `pgadmin_qt.exe`. Configuration and saved passwords
retain their usual per-user locations; this is not a portable-settings mode.

Local validation on 9 November 2026: the shared build script completed, the staged
application started, and the ZIP passed its CRC check. No test/settings files were
found in the archive. This is not yet clean-machine Windows validation, signing,
or a complete distribution acceptance test. The GitHub-hosted workflow has not
been executed from this workspace.
The workflow passes actionlint 1.7.12. The pinned aqt revision resolves the Qt
6.11.1 download command successfully in dry-run mode; a fresh hosted SDK installation
and the complete hosted job still need their first GitHub run.

## Windows installer

`tools/install-inno-setup.ps1` downloads Inno Setup 7.1.1 x64 from the official
release, verifies its fixed SHA-256 and installs the compiler into `build/tools`.
This is a build-machine tool, not a dependency installed on application users'
machines. It is intended for disposable hosted runners or explicit local installer
development. The product installer does not download or install PostgreSQL services.

`packaging/windows/alt-pg-admin.iss` consumes the same validated staging directory
as the ZIP. It installs for the current user under LocalAppData/Programs, without
requesting administrator rights. It provides Dutch/English/German wizard text,
the application icon, a Start menu shortcut, an optional desktop shortcut and
an uninstaller. A fixed AppId lets subsequent versions reuse the installation.
Settings, query files and vault entries are not part of the installer payload and
are not deleted on uninstall. The installer does not forcibly close a running app.

`-TestInstaller` builds an isolated test identity and installs into a fresh build
fixture, checks native startup, reinstalls to restore a missing DLL, and uninstalls.
It verifies that separate settings and an untracked SQL file survive. It never
uses the production AppId, production settings paths or existing password entries.
On 10 November 2026 these checks passed locally, together with all eight release
assembly checks and workflow actionlint validation. The generated installer is
unsigned; clean-machine/hosted Windows acceptance still needs verification.

## Linux jobs

`.github/workflows/linux.yml` is called by the main workflow and adds a Debian
12/13 container matrix for amd64 and ARM64, plus a Flatpak x86_64 job. ARM64 uses
native `ubuntu-24.04-arm` runners and targets 64-bit Raspberry Pi systems with
matching OS dependencies; 32-bit armhf builds are not configured. Package and
report artifact names include the architecture. It can also be run manually
for Linux-only builds, which do not publish a release. See [Linux packaging](LINUX-PACKAGING.md) for build/install commands,
permissions, client-tool versions and validation limits.

## macOS job

`.github/workflows/macos.yml` builds an Apple Silicon Release app and DMG with the
shared version, Qt, PostgreSQL client tools and their runtime libraries. It checks
the actual mounted DMG before uploading it. The initial target is macOS 15 or newer;
Intel is not configured. Native execution awaits the first hosted run. See
[macOS packaging](MACOS-PACKAGING.md) for deployment and signing limits.

## Remaining platform work

- Windows: validate the configured installer job on a hosted runner and perform
  clean-machine checks before a public release; configure signing if needed.
- macOS: confirm the first native build/DMG run and installed-app database/keychain
  behaviour; Developer ID signing/notarisation need later release configuration.
- Linux: run the new ARM64 Debian and Debian 13/Flatpak hosted jobs and validate database networking,
  local sockets, backup/restore and credential storage on installed desktop packages.

The macOS job is configured for Apple Silicon and is not yet validated natively.
Linux jobs are configured; Debian 12 has local
build/package validation, while Debian 13 and Flatpak still need hosted validation. Keep platform scripts
usable locally, and have each Actions job call those scripts. Cross-platform release uploads are configured and wait for every platform build.

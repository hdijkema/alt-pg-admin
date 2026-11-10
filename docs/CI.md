# GitHub Actions builds

GitHub Actions is the intended build service for distribution. The first configured
job is Windows x64 in `.github/workflows/build.yml`. It runs on pushes, pull
requests and manual dispatches, with read-only repository permissions. The workflow
belongs at the root of the `alt-pg-admin` Git repository.
The separate release-publishing job has `contents: write`; the build job stays read-only.

## Windows job

The `windows-2022` runner installs Python 3.12 and aqtinstall from upstream commit
`5a49f2b391ae8ee1d3c0dd87f8b33c1d49a4deb7`, downloads
Qt 6.11.1 MSVC 2022 x64 (qtbase, qtsvg and qttools archives) and the existing
PostgreSQL 18.6-5 SDK/test tools, then calls:

```powershell
.\build-release.cmd '-DPGADM_BUILD_TESTS=OFF' '-DPGADM_RELEASE_VERSION=0.2.0'
.\tools\package-windows.ps1 -QtRoot $env:PGADM_QT_ROOT -Version 0.2.0
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
`alt-pg-admin-windows-x64.zip`. The startup report is uploaded even after a failed packaging step
and retained for 14 days. Database data directories and application/user settings
are excluded. Ordinary branch/PR builds produce Actions artifacts only.

## Versioned releases

There are two explicit ways to publish a release after a successful Windows build:

- Push a numeric tag such as `v0.2.0`.
- Open Actions > Release build > Run workflow, select the source branch
  and enter `0.2.0` in the version field.
  An optional `v` prefix is accepted. Leaving the field empty builds only.

Use `major.minor.patch` without leading zeroes or prerelease suffixes. Invalid
versions fail before dependency downloads. A manual run on a tag must request
that same tag's version. PRs and ordinary branch pushes never publish a release.

The version becomes `PGADM_RELEASE_VERSION` in CMake, the application/About version,
and the archive name, e.g. `alt-pg-admin-0.2.0-windows-x64.zip`. The startup smoke
report verifies the executable version before a versioned archive is created.
Without a release override, the current development version is 0.1.0.

The publishing job downloads the successful build artifact, checks an existing
tag against the built commit, then uses `gh release create` with generated release
notes and the ZIP asset. A manual release creates a missing tag at that commit.
Existing releases are not overwritten; use a new version for a new release.
All action runtimes, including download-artifact v7, use Node.js 24.

The release workflow has been checked locally with actionlint and the resolver's
positive/negative cases (`tests/test-release-version.ps1`). Publication itself has
not been tested or performed from this workspace.
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

## Remaining platform work

- Windows: add the Inno Setup installer from the same staging directory, app
  branding, and clean-machine checks before a public release.
- macOS: add a separate native build job with matching Qt/PostgreSQL dependencies,
  tests, deployed app bundle and DMG. Select supported architectures explicitly;
  signing/notarisation require later release configuration.
- Linux: add the Flatpak manifest and a Linux job using a maintained KDE/Qt runtime,
  PostgreSQL 18 tools, and sandbox tests for networking, the credential store and
  file portals. Validate on the target desktop distributions.

macOS and Linux jobs are not configured or validated yet. Keep platform scripts
usable locally, and have each Actions job call those scripts. Add versioned release
uploads after packages and their platform checks are ready.

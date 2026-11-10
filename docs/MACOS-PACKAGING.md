# macOS DMG

The initial target is Apple Silicon (arm64), macOS 15 or newer. Intel/universal
builds are not configured. `.github/workflows/macos.yml` runs on `macos-15`,
checks its native architecture, and can also be dispatched for a build-only run.
The main workflow supplies the common version and waits for the DMG before release.

## Build and deployment

The runner installs pinned Qt 6.11.1 with the same aqt revision as Windows.
Homebrew supplies Ninja, PostgreSQL 18 client tools/SDK (`libpq@18`) and
dylibbundler. These are build-machine tools; end users need no Homebrew.
The scripts do not install or start a PostgreSQL server.
The Qt SDK includes qtdeclarative because the macOS lrelease binary links QtQml.
The workflow runs lrelease before compiling application sources, so missing
translation-tool frameworks fail early. This does not add QML to the widget
application's linked modules or deliberately deploy QtQml in the DMG.

`tools/build-macos.sh` builds Release, checks the translation catalogs and window
UI without database/keychain integration tests, then calls `tools/package-macos.py`.
The latter copies the bundle as `alt-pg-admin.app`, retaining its version, desktop
identity and ICNS icon. Five PostgreSQL client tools go into Contents/MacOS,
where PgTools already looks. dylibbundler copies their library dependencies to
Contents/Frameworks and rewrites references; the app uses that same bundled libpq.
macdeployqt then deploys Qt frameworks/plugins. Application and available Homebrew
dependency notices accompany the bundle.
Qt's Dutch/German base catalogs are also bundled for standard dialog/button text;
application translations remain embedded in the executable.

Bundled Mach-O files must support arm64. Build-machine rpaths are removed and
absolute library dependencies must be system libraries. The bundle is ad-hoc
signed and verified. Developer ID signing and Apple notarisation are not configured
and require later Apple release credentials. CI success does not establish
Gatekeeper acceptance for a quarantined Internet download.

The DMG contains the app and an Applications shortcut for drag-and-drop installation.
Packaging verifies and mounts the actual DMG read-only, checks native startup,
version and icon rendering in all three languages with isolated settings and a
reduced PATH, then checks each PostgreSQL tool. It detaches the image in a finally
block. Failed checks prevent copying the DMG to `dist` or uploading it as a package.

## Output and local build

A versioned build creates `alt-pg-admin-0.2.0-macos-arm64.dmg` and the Actions
artifact `alt-pg-admin-macos-arm64`. Branch builds omit the version in the filename.
Reports are separate artifacts. Only the main versioned workflow publishes a
release, after all six required packages succeed.

On a development Mac with the matching Qt SDK:

```sh
brew install ninja libpq@18 dylibbundler
PGADM_QT_ROOT=/path/to/Qt/6.11.1/macos PGADM_RELEASE_VERSION=0.2.0 \
  bash tools/build-macos.sh
```

## Validation

On 10 November 2026 workflow actionlint, shell/Python syntax and eight release
helper checks passed locally. The Qt download command resolves the expected SDK
in dry-run mode. The supplied ICO was preserved; ICNS was regenerated from the SVG.
No native Mac build, bundle execution or DMG mount was performed in this Windows
workspace. The first hosted job must confirm deployment, Cocoa startup, dependencies
and DMG integrity. Installed-app database, backup/restore and Keychain behaviour
still require platform acceptance tests.

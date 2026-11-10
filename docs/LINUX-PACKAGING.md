# Linux packages

The main `.github/workflows/build.yml` calls `.github/workflows/linux.yml` on
pushes and pull requests, and for manual release builds. The Linux workflow can
also be run directly for a Linux-only build.
It produces five downloadable package artifacts, retained for 14 days:

| Artifact | Contents |
| --- | --- |
| `alt-pg-admin-debian12-amd64` | A `.deb` built against Debian 12's Qt and libpq |
| `alt-pg-admin-debian13-amd64` | A `.deb` built against Debian 13's Qt and libpq |
| `alt-pg-admin-debian12-arm64` | A native ARM64 `.deb` built against Debian 12's Qt and libpq |
| `alt-pg-admin-debian13-arm64` | A native ARM64 `.deb` built against Debian 13's Qt and libpq |
| `alt-pg-admin-flatpak-x86_64` | An installable `.flatpak` bundle |

The build jobs have read-only repository permissions and do not publish to a
package repository or Flathub. For a numeric version tag such as `v0.2.0`, or a
manual main workflow run with version `0.2.0`, all platforms use that same version
and the main release job attaches the Windows ZIP/installer, all four `.deb` files, Flatpak and the macOS DMG
to one GitHub Release. The release job waits for every platform build to succeed.
Ordinary branches/PRs and direct manual Linux-only runs produce Actions artifacts.
An empty version uses the CMake development version.

## Native Debian packages

Each job builds in a matching `debian:12` or `debian:13` container, using the
normal system libpq (15 or newer). The amd64 jobs use `ubuntu-24.04`; ARM64 jobs
use the native `ubuntu-24.04-arm` runner, without emulation or cross-compilation.
The workflow checks the container and package architectures against the matrix.
Artifact and report names include the architecture; each isolated job keeps its
build files in `build/debian12` or `build/debian13`.
There is no bundled PostgreSQL library or
server. CPack calculates shared-library dependencies and adds the Qt SVG/platform
plugins, Qt translations, Secret Service client library and `postgresql-client`.
On Debian 13, `qt6-svg-plugins` is an explicit build/runtime dependency: unlike
Debian 12 it is not included in `libqt6svg6`. It provides SVG icon decoding when
recommendations are disabled. The build container and script explicitly use
`C.UTF-8` to avoid Qt's C-locale warning. Startup checks name each language and
report an icon-rendering failure explicitly.
The client tools support backup, restore and database tasks; use client tools at
least as new as the server for backups. A server newer than the distribution's
client tools may require a separately installed matching PostgreSQL client.

The package installs `/usr/bin/pgadmin_qt`, a desktop launcher, icon, application
metadata and licence notices. It has the host access of a normal desktop process,
under the current user's OS permissions. Database privileges still come from the
PostgreSQL account. It does not install or configure a PostgreSQL service.

Local build on Debian 12 or 13:

```sh
sudo apt-get install build-essential cmake ninja-build pkg-config python3 \
  qt6-base-dev qt6-l10n-tools qt6-svg-dev qt6-translations-l10n libpq-dev \
  libsecret-1-dev dpkg-dev file fonts-dejavu-core
./tools/build-debian.sh
sudo apt-get install ./dist/alt-pg-admin_*-debian12_amd64.deb
```

Use the matching `debian13` filename on Debian 13. `PGADM_RELEASE_VERSION=0.2.0`
sets a release version; extra arguments to the script go to CMake.
`CMAKE_BUILD_PARALLEL_LEVEL` controls build parallelism (default: 2).

### Raspberry Pi / ARM64

The ARM64 packages target a Raspberry Pi running a **64-bit** Debian-based desktop
OS with matching Debian 12 (Bookworm) or 13 (Trixie) dependencies. A 64-bit CPU
alone is not sufficient: `dpkg --print-architecture` must report `arm64`.
32-bit `armhf` systems are not covered by these builds. Choose the package for
the installed OS release, for example on a Debian 13-based ARM64 system:

```sh
sudo apt-get install ./alt-pg-admin_0.2.0-debian13_arm64.deb
```

The same local build script detects the native package architecture automatically.
The ARM64 jobs run the same tests and installed-package startup checks as amd64.
Actual Raspberry Pi desktop, graphics and credential-storage checks remain pending.

CI tests local connection failures, editor behavior, CSV/XLSX output, language
selection/persistence and three language startup checks. It installs the actual
`.deb` and checks the installed binary and client tools as well. Database integration
and desktop-keyring round trips require a dedicated server/session and are excluded
from this headless package check. No existing databases or credentials are used.

## Flatpak

The manifest is `packaging/flatpak/io.github.hdijkema.AltPgAdmin.json`. It uses the
KDE 6.10 runtime and includes PostgreSQL 18.6 client tools and libpq. Only client
libraries/tools are installed; no database service is included. QtKeychain, zlib
and libxlsxwriter use the same versions and hashes as the normal CMake build.
`tools/prepare-flatpak.py` generates the CI manifest with the requested CMake
version without changing the checked-in manifest. All sources are downloaded before compilation; FetchContent is given local source
directories, so compilation does not require network access in the build sandbox.

The hosted runner configures Flathub system-wide at
`https://dl.flathub.org/repo/flathub.flatpakrepo` and explicitly installs both KDE
6.10 SDK and Platform system-wide before invoking the builder action. The bundle
is also installed system-wide for its CI checks. This matches flatpak-builder's
default dependency-installation scope; a user-only remote cannot satisfy its
system installation lookup. The SDK ref is checked before the large SDK download.
The action's upstream Node `punycode` deprecation warning is separate from this
repository-scope failure and is not hidden by disabling warnings.

The installed application remains a Flatpak sandbox, with explicit access to:

- Networking, including localhost and remote PostgreSQL servers.
- Host files for SQL files, exports, backups/restores and certificates, under the
  current user's filesystem permissions (`--filesystem=host`). Flatpak's reserved
  system paths still apply; this is not a privileged host shell.
- `/run/postgresql` for Debian's local PostgreSQL Unix sockets. Custom socket paths
  may require an extra `flatpak override --user --filesystem=/custom/path` grant.
- The desktop Secret Service and KWallet 6 for remembered passwords.
- Wayland, with X11 fallback, and graphics acceleration.

Bundled tools run inside the sandbox. Host commands and system service management
are not exposed through `flatpak-spawn --host`. Use the native Debian package for
unrestricted host-program access. Database administration itself uses PostgreSQL
connections and is governed by the connected role's privileges.
Settings remain in the Flatpak's per-application XDG directory; native profiles
are not automatically imported. On Linux, Secret Service/KWallet must be available
in the user's desktop session to remember passwords; plaintext fallback stays off.

Install the CI bundle with Flathub configured for the runtime (versioned builds
add the version to the filename):

```sh
flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install --user ./alt-pg-admin-linux-x86_64.flatpak
flatpak run io.github.hdijkema.AltPgAdmin
```

Local build with `flatpak` and `flatpak-builder` installed:

```sh
flatpak-builder --user --install-deps-from=flathub --force-clean --repo=build/flatpak-repo \
  build/flatpak-app packaging/flatpak/io.github.hdijkema.AltPgAdmin.json
flatpak build-bundle build/flatpak-repo dist/alt-pg-admin-linux-x86_64.flatpak \
  io.github.hdijkema.AltPgAdmin
```

CI installs the bundle and runs startup checks for all languages and `--version`
checks for pg_dump, pg_restore, psql, createdb and dropdb. A full interactive check
of localhost/socket access, backup/restore and desktop credential storage is still
needed on the installed Flatpak. Flathub submission and signing are separate work.

## Validation status

The Debian 12 application, tests and `.deb` package were built locally. Translation
catalogs, desktop metadata, workflow syntax and the Flatpak manifest schema are
checked locally as well. Debian 13 and the full Flatpak build have not yet run in
this workspace; the hosted workflow provides their first complete package checks.
Both ARM64 Debian jobs are configured but have not been executed from this
workspace. Native ARM CI and Raspberry Pi hardware validation are still required.
On 10 November 2026 the KDE SDK 6.10 ref was reachable at the official Flathub
repository and the corrected workflow passed actionlint. Full Flatpak build and
sandbox checks remain pending a hosted run; no local Linux environment was used.

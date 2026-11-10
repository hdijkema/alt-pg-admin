# Linux packages

`.github/workflows/linux.yml` builds on pushes, pull requests and manual runs.
It produces three downloadable Actions artifacts, retained for 14 days:

| Artifact | Contents |
| --- | --- |
| `alt-pg-admin-debian12-amd64` | A `.deb` built against Debian 12's Qt and libpq |
| `alt-pg-admin-debian13-amd64` | A `.deb` built against Debian 13's Qt and libpq |
| `alt-pg-admin-flatpak-x86_64` | An installable `.flatpak` bundle |

These jobs have read-only repository permissions and do not publish to a package
repository, Flathub or GitHub Releases. The existing Windows release workflow is
independent. Linux artifacts use the CMake development version by default.

## Native Debian packages

Each job builds in a matching `debian:12` or `debian:13` container, using the
normal system libpq (15 or newer). There is no bundled PostgreSQL library or
server. CPack calculates shared-library dependencies and adds the Qt SVG/platform
plugins, Qt translations, Secret Service client library and `postgresql-client`.
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
All sources are downloaded before compilation; FetchContent is given local source
directories, so compilation does not require network access in the build sandbox.

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

Install the CI bundle with Flathub configured for the runtime:

```sh
flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install --user ./alt-pg-admin-x86_64.flatpak
flatpak run io.github.hdijkema.AltPgAdmin
```

Local build with `flatpak` and `flatpak-builder` installed:

```sh
flatpak-builder --user --install-deps-from=flathub --force-clean --repo=build/flatpak-repo \
  build/flatpak-app packaging/flatpak/io.github.hdijkema.AltPgAdmin.json
flatpak build-bundle build/flatpak-repo dist/alt-pg-admin-x86_64.flatpak \
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

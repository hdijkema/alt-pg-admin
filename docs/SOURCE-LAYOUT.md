# Source layout

Status: implemented on 10 November 2026. Application code lives under `src`.

Files are grouped by their existing responsibilities. Classes and ordinary helper
functions retain their behaviour and ownership. There is no separate library target
for each folder. The application remains one target,
with the existing small qtxlsxwriter target.

```text
pgadmin-jr/
  CMakeLists.txt
  README.md
  LICENSE
  build-release.cmd
  .github/workflows/
  src/
    app/
    connections/
    database/
    browser/
    query/
    results/
    administration/
  assets/
    icons.qrc
    icons/
  cmake/
  docs/
  tests/
  tools/
```

In the table, a basename includes its existing .cpp/.h pair unless noted.

| Folder | Files | Responsibility |
| --- | --- | --- |
| `src/app` | `main.cpp`, `pgadmmain`, `pgadmmain.ui`, `editorstate.cpp` | Application/window composition, tab/session dispatch and window/editor persistence. |
| `src/connections` | `profiledialog`, `passwordstore` | Connection profile input and system credential-store access. |
| `src/database` | `pgsession`, `sqlscript`, `pgtools`, `catalogservice` | libpq workers, script parsing, PostgreSQL tool discovery and catalog requests. |
| `src/browser` | `catalogtree`, `objectdetails` | Object tree and property/definition widgets. |
| `src/query` | `querytab`, `sqleditor`, `sqlhighlighter`, `querylibrary`, `savedqueriespanel` | SQL editing, saved queries and query-tab UI. |
| `src/results` | `queryresult.h`, `resultmodel`, `cellviewer`, `resultexporter`, `fullresultexport`, `qtxlsxwriter` | Result data, display, inspection and retained/full streaming export. |
| `src/administration` | `databasejob`, `databasetaskdialog`, `rolesql`, `rolemanager` | Backup/restore, database create/drop, maintenance and role administration. |

`assets/icons.qrc` contains paths relative to `assets` and keeps the existing
`:/icons/...` resource aliases. Generated builds, downloaded SDKs and package
output stay outside `src` and are ignored by Git. Third-party code remains under
the existing generated/downloaded dependency locations.

## Includes and build targets

`src` is the application/test include root. Cross-folder includes are qualified,
for example `database/pgsession.h` or `results/resultmodel.h`; same-folder includes
retain their short names. Generated moc/UI includes keep their Qt-generated names.
Individual source subfolders are not added as global include directories.

CMake lists application and test sources explicitly using their new paths. The
browser test excludes `src/app/main.cpp` and `src/app/pgadmmain.ui` from the shared
application source list. The qtxlsxwriter target exposes the same `src` include
root to its users and continues to hide libxlsxwriter's C API.

Build scripts, workflow commands and package output locations retain their existing
paths. The structural change does not introduce plugin systems, factories,
component frameworks or standalone packages.

## Validation

On 10 November 2026 the Windows Release application and all three existing test
targets built from the new paths. The local disposable PostgreSQL 18 suite passed
45 checks (11 connection, 13 query/export, 21 browser/editor/maintenance), with
none failed or skipped. The temporary server was stopped. Reports remain under
`build/Release/integration-1fb330e37400467db383c9ff5606a2cf`.

The portable ZIP passed native startup, version and archive integrity checks.
The release-version checks and GitHub workflow actionlint validation also passed.
The hosted workflow still excludes PostgreSQL integration tests. These checks
validate Windows locally; macOS/Linux and clean-machine distribution remain
unvalidated. No GitHub release was published during this structural change.

# PgAdm Qt

A native PostgreSQL administration application built with C++17 and Qt Widgets.
It provides connection profiles, a lazy object browser, query execution, saved
queries, result export, server roles and database maintenance tasks.

See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) for component responsibilities and
data flow, [the implementation plan](../STAPPENPLAN.md) for remaining work, and
[the architecture analysis](../ANALYSE-QT.md) for the original design rationale.
Technical documentation and source comments are written in English. UI labels
quoted below retain their Dutch spelling so they can be found in the application.

## Quick start

1. Run `build/Release/pgadmin_qt.exe`, or build/run through Qt Creator.
2. Choose **Verbinding > Nieuw profiel** and enter the host, port and user.
   Normally use `postgres` as the maintenance database; the user may also be
   `postgres`, depending on your server configuration.
3. Select the SSL mode and connection timeout (default: 10 seconds).
4. Select the profile and choose **Verbinden**, or double-click the profile.
5. Enter a password if requested. **Wachtwoord onthouden** saves it in the system
   credential vault after a successful connection. Existing libpq facilities such
   as pgpass are also available.
6. Expand **Databases**, right-click the desired database and select
   **Querytab openen**. The new query tab targets that database.
7. Enter SQL and press F5. A selection executes only the selected text; otherwise
   the entire script executes. **Query annuleren** cancels the running script.

The connection selector above the editor identifies both server and database.
Selecting a different tree node does not retarget an existing query tab. Ctrl+N
inherits the current query tab's database. Disconnecting leaves that target
visible but unavailable; the application does not silently switch databases.

Profiles can be edited or removed when they are not in use. Several profiles may
be connected at once. The status bar shows connection state; the server tooltip
includes the server version and backend PID. No server connections, catalog
queries or vault reads happen automatically at startup.

## Password storage and TLS

The profile editor accepts a password and a remember-password preference. An empty
password field keeps the existing saved secret. Clearing the preference and saving
removes it. A newly entered password without persistence is kept in memory for the
next connection. Deleting a profile also removes its credential entry.

QtKeychain uses the system vault, with plaintext fallback disabled. Only the
remember preference is stored in settings.ini. Vault keys include profile, host,
port and user, allowing password reuse across databases on the same server.
Changing the host or user selects a different key. Vault failures are reported.

SSL modes are `prefer`, `require`, `verify-ca`, `verify-full` and `disable`.
Certificate files currently use libpq's normal configuration. Certificate pickers
and SSH tunnels are not implemented. A successful connection with a custom TLS
certificate chain has not been independently validated.

## Database and object browser

The lazy tree follows this structure:

```text
server > Databases > database > schema
    > Tables / Views / Materialized views / Sequences > object > columns
```

Children are fetched when expanded. Template databases and system schemas are
currently hidden. Databases without CONNECT permission or with connections
Disabled remain visible but cannot be opened. Schemas require USAGE permission.
Use the context menu or **Verbinding > Objectboom vernieuwen** to refresh changes
made outside this application.

Right-click or double-click a database to open a query tab, including when that
database is already connected. The profile's maintenance database is unchanged:
a maintenance connection to postgres can coexist with query connections elsewhere.
Query tabs targeting the same server/database currently share one session and its
transaction. One script may run at a time per session. There are at most eight
query sessions, plus up to eight separate metadata connections. Idle metadata
connections may be replaced when needed. A long query does not occupy the
browser's metadata session.

### Object properties and definition SQL

Right-click a database, schema, relation or column and choose
**Eigenschappen / definitie-SQL**. The **Objecteigenschappen** dock follows the
selection while visible. **Vernieuwen** reloads the selected properties.

For tables, views, materialized views, sequences and columns, the
**Definitie-SQL** tab runs the bundled pg_dump on demand. A column shows its parent
relation's definition. Database and schema nodes currently show properties only.
Definition SQL can be copied or saved, but is never executed automatically.

This is a schema-only pg_dump script without data, ownership or privilege commands.
It may contain psql directives unsupported by the query editor. A single object's
script does not guarantee inclusion of all required schemas, types or dependencies
and is not a full backup. Permission failures, missing objects/tools and tool
errors are displayed. The lock timeout is two seconds; the overall timeout is
30 seconds. Changing selection or hiding the dock cancels obsolete work.

## Query editor and results

- Ctrl+N/Ctrl+W open and close tabs; Ctrl+O/Ctrl+S open and save UTF-8 SQL files.
- SQL highlighting recognises keywords, strings, quoted identifiers, numbers,
  nested block comments and PostgreSQL dollar-quoted bodies.
- Unsaved changes are marked and checked before closing or replacing a document.
- The editor/result split and dock positions are adjustable and persisted.
- The compact toolbar uses embedded Lucide SVG icons. Hover for action names and
  shortcuts; menu labels remain available. The ISC/MIT notices are included in
  assets/icons/LICENSE.txt and deployed as Lucide-LICENSE.txt.

Script statements run separately, in order, stopping at the first error. Without
explicit BEGIN/COMMIT, each successful statement commits separately. Completion
reports the actual transaction state, including when ROLLBACK is needed. Query
cancellation skips subsequent statements.

The result area is one native QTableView. Section names, column headings, data
and completion messages continue vertically even when statements have different
columns. Separate result tabs are not created per statement.

- **Tussenresultaten tonen** includes intermediate results (enabled by default).
  Disabling it retains only the final result on the next execution.
- Add `-- @resultaat: Customer overview` before a statement to name its section.
- **Kopi?ren** copies the displayed overview as TSV and escaped HTML, including
  headings, stored full cell values and status messages.
- **CSV opslaan...** writes UTF-8 with a BOM, comma delimiters, quoted fields and
  doubled quotes. One result is a plain table; multiple results retain sections.
- **Excel opslaan...** writes one XLSX overview sheet with the selected widths.
  Values are explicit strings, preserving leading zeroes, large numbers and text
  beginning with `=`. Cells exceeding 32,767 UTF-16 code units, or containing NUL,
  fail with a message suggesting CSV. Existing output files remain intact.
- Exports run in the background from the stored results. They do not rerun SQL.
  Truncation is marked and requires confirmation before export. NULL appears as
  `NULL`; bytea uses hexadecimal `\x...` notation.
- EXPLAIN output is included in the result overview. **Uitvoeringsplan** also
  displays the latest available plan; **Meldingen** lists notices and errors.

The splitter handles ordinary/E-strings, double quotes, dollar quotes, line
comments and nested block comments. It is not a complete PostgreSQL grammar
parser. psql directives and COPY data streams are unsupported in the query editor;
a COPY stream closes the connection with an explanation. Keep
`standard_conforming_strings=on`; execution stops if it is disabled. Compound SQL
bodies without dollar quoting are not a supported script case.

### Saved queries and categories

The **Bewaarde queries** dock stores named scripts locally. Use **Huidige opslaan...**
or Ctrl+Alt+S, then double-click or press Enter to reopen one. An already-open
query receives focus so unsaved edits survive. Rename/delete actions are in the
same panel. Deleting a saved query leaves its open text as an unsaved document.
The dock can be shown or hidden through **Beeld**.

Use the category selector for **Alle categorie?n**, **Zonder categorie** or a named
category. Select a query and choose **Categorie...** to select an existing category
or type a new one. An empty category removes the assignment. Search applies inside
the selected category. Newly saved queries inherit the active category; existing
queries keep theirs. Empty categories disappear. Query names remain unique across
the library, case-insensitively.

Scripts are ordinary UTF-8 SQL files under `queries` beside settings.ini. Their
filenames encode the query names. Category metadata is in `queries/categories.ini`;
include that file when copying the library. Startup lists names and categories,
loading SQL only when opened. Categorisation changes neither SQL text nor paths.

Drag result header boundaries to resize columns, double-click for content sizing,
or use the header context menu for exact widths, all-column sizing or defaults.
Widths are stored by column position per saved query/SQL file, including across
restarts and query renames. Unsaved tabs use the general default. Layout metadata
is stored separately from SQL.

### Large cells and storage limits

Cells show about 200 characters or a short binary preview. Single-click for the
side viewer; double-click for a dialog. Text wraps, including long words. Text is
paged in 50,000-character chunks and binary hex in 4 KiB chunks. Copy/save uses the
stored full value, and saving bytea writes its original bytes.

Stored results are limited to 20,000 rows per statement, 50,000 per script and
approximately 32 MiB including per-cell overhead. Each cell retains up to 2 MiB of
UTF-8 input; bytea's encoded input consumes this budget, reducing retained binary
bytes. The remaining overall budget may lower these limits. Truncation remains
visible, including in clipboard/export output. Saving a truncated cell asks for
confirmation that only its available portion will be written.

The client drains/counts remaining rows; limits do not rewrite SQL or automatically
reduce server work/network traffic. Single-row libpq mode avoids collecting the
entire result, but one extremely large row may temporarily exceed the storage
budget. Worker-to-GUI batches use acknowledgement-based backpressure.

OID values are displayed as values, not assumed to reference PostgreSQL large
objects. On-demand large-object loading/export via OID remains future work.

## Database maintenance

The connected server/database context menu provides:

- **Database aanmaken...**: create an empty UTF-8 database from template0, owned by
  the connected user.
- **Databasebackup...**: dump the complete database, including data, as a custom
  PostgreSQL archive or plain SQL. This uses pg_dump directly and is unaffected by
  query display limits. A database dump does not create server roles.
- **Database herstellen...**: restore an archive with pg_restore or a SQL file
  with psql into the explicitly displayed database, preferably empty. Archive
  restore uses one transaction and stops on errors. Skipping ownership/ACLs is on
  by default; cleaning existing archive objects is off. Those switches apply only
  to archives. SQL files define their own SQL/psql commands; standard pg_dump SQL
  is executed transactionally, but arbitrary scripts can override transactions.
- **Database verwijderen...**: type the exact database name to confirm. postgres,
  template0/template1 and the configured maintenance database are protected.
  External clients are not forcibly disconnected; PostgreSQL may refuse the drop.

Server-level actions select a database from the loaded list. The modeless task
window captures its target independently of later tree selections. **Starten**
confirms execution; log output and **Annuleren** remain available during work.
**Verstreken tijd** updates during the task; **Totale duur** remains after success,
failure or cancellation, including milliseconds. Backup timing includes final file
publication. The progress bar is indeterminate, with no fabricated percentage/ETA.
Failed/cancelled backups preserve existing files. CREATE/DROP may already have
committed when cancellation arrives; inspect the refreshed tree afterward.

Restore/drop close this application's idle target sessions and metadata connections,
rolling back open transactions as disclosed by the dialog. Running queries must
first finish or be cancelled. The target is unavailable for new local connections
during maintenance. Editors retain their SQL and explicit database targets.

## Server roles and database rights

Choose **Rollen en databaserechten...** from a connected server/database context
menu. Roles apply to the entire server. Create roles or edit LOGIN, passwords,
expiry, connection limits, administrative attributes, comments and memberships.
Existing role names cannot currently be renamed/deleted in this editor. Built-in
pg_ roles are read-only. New roles default to NOLOGIN without administrator rights.
Review the SQL preview and select **Toepassen** to apply changes in a private
transaction, separate from query-tab transactions. Apply/discard one tab's changes
before switching to the other editable form. Server permissions remain authoritative.

Passwords are not shown in SQL previews or saved settings. libpq prepares a local
SCRAM-SHA-256 verifier before sending password-change SQL. If changing the password
of the profile's own user, update that connection profile separately.

The **Databaserechten** tab distinguishes direct CONNECT/CREATE/TEMPORARY grants,
GRANT OPTION and effective privileges. PUBLIC, memberships or ownership can retain
effective access after a direct grant is revoked. Changing database ownership is
an explicit option. This editor does not grant table or schema privileges.

A SQL dump may reference owner/grantee roles without creating them. If restore
fails because `sparxea`, for example, is absent, choose **Rollen beheren...** in
the restore dialog, review/create that role, and start a new restore from the
context menu. An ownership-only role can remain NOLOGIN; authenticating as it
requires LOGIN and suitable credentials. SQL dumps are not rewritten to remove
ownership/ACL commands.

## Dependencies and build

Currently validated: Windows, Qt 6.11.1 MSVC 2022 x64, CMake 3.24 or newer and Ninja.
The standalone build script contains this machine's Qt/Visual Studio paths.

- PostgreSQL 18.6-5 SDK/runtime from the EDB binary distribution, stored locally.
  The application uses libpq directly, with nonblocking I/O in a worker thread.
- Qt Network for asynchronous hostname lookup; Qt Concurrent for background export.
- QtKeychain 0.17.0, statically built with plaintext fallback disabled.
- Libxlsxwriter 1.2.4 and zlib 1.3.2, statically built. The small `QtXlsxWriter`
  bridge accepts Qt text rows and widths and safely publishes the workbook.
  No private Qt module or additional XLSX runtime DLL is required.

CMake FetchContent pins source archives by SHA-256. Initial configuration requires
network access; subsequent builds reuse cached sources. Licences are deployed
beside the application. Minizip/tmpfileplus sources bundled with libxlsxwriter are
covered in its licence notice; optional MD5 image deduplication is disabled.

Fetch the PostgreSQL files, without installing a service or changing PATH:

```powershell
py -3 tools\fetch-postgresql.py
```

The downloader fetches selected parts of the official archive into
`third_party/postgresql18`; manifest.json records versions, URLs and hashes.
It also obtains server/tools used for isolated integration tests. The app itself
uses client DLLs and PostgreSQL maintenance tools. Python is a download/test tool,
not an application runtime dependency. QPSQL is available in Qt but is not used;
it would also require libpq and its runtime DLLs.

Open CMakeLists.txt in Qt Creator with the Qt 6.11.1 MSVC 2022 64-bit kit. Configure
`PGADM_POSTGRESQL_ROOT` to select another PostgreSQL 18 SDK/runtime root.
For a standalone Release build:

```powershell
.\build-release.cmd
```

The script loads Visual Studio, runs CMake/Ninja and windeployqt. CMake deploys
libpq, its runtime DLLs, pg_dump, pg_restore, psql, createdb and dropdb, including
lz4/zstd dependencies. Keep the deployed files together. Clean-machine distribution
has not yet been validated.

## Settings and startup measurement

Settings use Qt AppConfigLocation for Hans/PgAdmQt, in settings.ini. Profiles do not
contain passwords. `--settings-file <path>` selects an isolated settings file.

`--startup-check --startup-report <path>` measures from main() until the first
event-loop callback after show(), then exits. Create the report directory first.
It excludes loader time before main() and does not prove that every pixel has
already been painted. Earlier warm-start measurements are not cold-start
benchmarks or a complete comparison with pgAdmin.

## Testing

Validated on 9 November 2026 after the libxlsxwriter migration: 36 checks passed
(11 connection, 11 query/export, 14 browser/maintenance), with no failures or
skipped tests. Reports: [integration-fdb538041da44d699cefc7173770300c](build/Release/integration-fdb538041da44d699cefc7173770300c/).
The disposable PostgreSQL server was stopped. The executable dependency list
confirms that neither libxlsxwriter nor zlib requires an extra runtime DLL.

```powershell
.\build-release.cmd -DPGADM_BUILD_TESTS=ON
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\run-integration.ps1
```

Test builds require Python 3 for independent XLSX inspection using only zipfile,
ElementTree and JSON from its standard library. CMake locates the interpreter.
The application itself never needs Python to export XLSX.

The integration script starts a disposable PostgreSQL 18 cluster on a free loopback
port and stops it in finally. Existing databases/services are not used. Unique
vault entries isolate credential tests and are removed afterward. Reports and
cluster artefacts remain under `build/Release/integration-*`. The offscreen Qt
platform plugin is required and checked before tests start. Windows font/toolchain
paths are explicit current test assumptions.

Coverage includes connection/authentication errors, cancellation, responsive I/O,
SQL splitting, transactions, multiple results, result limits, cell viewers,
clipboard, CSV/XLSX export, saved-query categories/widths, browser context actions,
metadata independence, definition dumps, backup/restore roundtrips, database tasks,
role changes, SCRAM passwords, direct database grants and rollback on errors.
XLSX checks inspect actual ZIP/XML output independently of the writer, including
Unicode paths, literal text/formulas, large numbers, headers and column widths.

Without PGADM_TEST_PORT, database-dependent tests are skipped; the integration
script supplies it automatically. The supplied performance-example SQL is parsed
as a fixture, not run against a user's database.

## Remaining work

The agreed priorities are maintenance (VACUUM/ANALYZE/REINDEX), independent query-tab
sessions/transactions, full result export beyond display retention limits, and
editor comfort (search/replace, history, tab restoration and completion).
Windows/macOS installers and Linux Flatpak builds follow, with GitHub Actions
proposed for separate platform builds/tests and packaging. Flatpak targets Debian,
Ubuntu and Linux Mint with Flatpak configured. macOS/Linux are not yet built or validated; the
current dependency downloader and integration scripts are Windows-specific.
Linux packaging must validate sandbox networking, credential storage and file
portals, and include PostgreSQL client/maintenance tools. Flathub publication is
a separate release step after installable CI artefacts have been tested.

Graphical object create/alter/drop editors are deferred because SQL covers the
current need. Table-data editing and extended object-privilege editors are not
immediate priorities. JSON plans, OID large objects and SSH/certificate UI are
optional future work. Monitoring, replication, ERD, debugger and schema diff remain
outside the requested scope. See the implementation plan for current ordering.

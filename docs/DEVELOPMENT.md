# Implementation guide

This project follows Hans's preference for direct, procedural code, adapted to
Qt's widget/model and asynchronous APIs. Stateless operations use functions;
classes own actual UI state, connection state or resource lifetimes. Do not add
an application framework or general repository/service layer around Qt.

## Startup and connection ownership

`main` constructs `PgAdmMain`. The main window reads local `QSettings`, creates
widgets and lists saved query filenames. It does not connect to PostgreSQL,
read the password vault or execute previous SQL during startup.

`ProfileDialog::edit` edits a local form. On cancellation it changes nothing;
on acceptance it returns profile fields and a separate password. `PgAdmMain`
persists the profile, calls `PasswordStore` when requested and manages live
sessions. `PasswordStore` delegates to QtKeychain with insecure fallback disabled.
Its key includes server ID, host, port and user, but not the database. SQL and
passwords must not be included in diagnostic connection strings or log messages.

`PgSession` is the GUI-thread facade. It owns a worker thread and forwards
commands to `PgConnection`. The worker owns the `PGconn`, DNS lookup, socket
notifier, connection timer and asynchronous cancellation state. Connection
completion emits information only after libpq has established the connection.
Shutdown stops the worker event loop and waits for its resource cleanup.

Each query tab owns a lazy private PgSession in PgAdmMain::querySessions. Its
logical server/database target stays separate in queryTargets. At first execution,
pendingScripts holds the immutable SQL until connection completes. Results,
notices, cancellation and transaction status route only to that tab. The eight-tab
connection limit is independent of the existing eight control and eight metadata
connections. Closing/retargeting releases the tab's session, with explicit rollback
confirmation for native open/failed transaction state. Restore/drop refuse busy
tabs and close idle target sessions; disconnecting a server/database covers all
its query tabs. Empty tabs require no backend. Catalog metadata uses separate sessions, so a user query or
transaction does not occupy the catalog queue. Do not silently change a tab's
database when its connection disappears.

## SQL execution and result storage

`QueryTab::sqlToExecute` returns the selection or entire editor text.
`PgAdmMain::runQuery` selects the tab's session and calls `startExecution` before
dispatching SQL. `SqlScript::split` recognises strings, quoted identifiers,
dollar bodies and nested comments; it is not a complete PostgreSQL parser.
The worker executes statements in order and stops on the first error or cancel.
Autocommit remains PostgreSQL's default unless SQL explicitly opens a transaction.

`PgConnection::readResults` drains libpq single rows into batches of at most
128 stored rows. `publish` waits for `PgSession` to acknowledge delivery before
reading more, bounding the queued UI work. Row, cell and byte limits bound stored
results. Additional server rows are drained and counted, not silently treated as
stored rows. A single incoming libpq row may still temporarily exceed that budget.
COPY streams are rejected by closing the connection to restore protocol state.

`ResultModel` stores `QueryResult` sections and a flat vector of row references.
It exposes headings, column names, values and status lines to one `QTableView`;
it creates no widgets per row. With intermediate results off, it retains only
the latest section. `ResultCell::preview` is deliberately short; `fullText`
preserves the stored value, including NULL and bytea distinctions in the struct.

`CellViewer` copies the selected cell and pages through its stored text or bytes.
Text wraps; binary hex rows keep their alignment. Copy/save never reconstruct
the value from the short table preview. Truncated values remain marked.

## Saved queries, widths and export

Full result export is an explicit execution path, not a rerun of stored results.
PgAdmMain::executeQuery captures SQL/path/format/widths in PendingQuery while a
lazy tab session connects, then calls PgSession::executeToFile exactly once.
The worker prepares FullResultExport before SQL and converts each libpq row before
applying preview limits. It never queues full rows to the GUI: only the existing
bounded preview batches and throttled row counts cross threads. FullResultExport
streams CSV through QSaveFile or XLSX through the small QtXlsxWriter bridge.

Section metadata comes from actual statement results, including empty SELECTs.
CSV is plain for one result and an overview for scripts; XLSX is always an overview.
Bytea is canonical hex, NULL is literal NULL and values are explicit strings.
The whole file commits only after complete script success. File errors request
query cancellation and stop subsequent statements; they do not roll back SQL that
already committed. Client failure and PostgreSQL transaction state remain distinct.

PgSession shares an atomic export cancellation flag with its worker. The GUI sets
it immediately, including when the worker is finalising a ZIP. QtXlsxWriter::save
checks it after ZIP generation and during the final copy, before publication.
Native cancellation still handles active SQL. QueryTab runs its own elapsed timer
so time keeps updating during ZIP finalisation; notices report final outcome.
One broad row/field can temporarily use substantial memory, but earlier full rows
are never retained. Normal saved-preview export and its clipping confirmation
remain unchanged.

The main toolbar reuses its existing QActions with 24-pixel Lucide SVG icons from
icons.qrc. Tool buttons show icons; QAction text continues to supply tooltips,
accessible names and menu labels. Native Qt rendering supplies disabled states.
Eight local assets and their upstream ISC/MIT licence are bundled; no icon
framework or runtime network request is introduced. Deployment needs Qt's SVG
icon-engine plugin, already included by windeployqt.

`QueryLibrary` maps names to percent-encoded UTF-8 SQL filenames. Listing reads
names only. `QSaveFile` replaces a file only after a successful write/commit.
`QueryLibrary` stores optional name-to-category assignments in categories.ini
through QSettings. Existing scripts without metadata are uncategorised. Categories
list only names assigned to surviving files, with case-insensitive spelling reuse.
Assignments leave SQL paths/text unchanged; query rename carries its category and
deletion removes its assignment. A metadata write failure during rename rolls the
SQL filename back when possible and reports a filesystem error.
`SavedQueriesPanel` combines a category filter (all/uncategorised/named) with name
search, and assigns categories through an editable picker. A new query inherits
the active named category; overwriting a query preserves its own category.
`PgAdmMain` owns
editor loading and dirty-state decisions. Reopening a named query focuses its
existing tab so unsaved edits are not discarded.

Column widths are stored by column position. A saved query/file uses a hashed
absolute-path settings key; unsaved tabs use the general default. Renaming a
library entry migrates its layout key; deleting it detaches open editors and
marks them modified. SQL text itself contains no layout metadata.

`SqlEditor` subclasses QPlainTextEdit for concrete editor interaction ownership.
Its modeless child dialog uses QTextDocument literal searches, with one wrapped
search and optional case/word flags. Replace-all advances past inserted text in
one QTextCursor edit block. Explicit QCompleter suggestions combine `sqlKeywords`
(also consumed by SqlHighlighter) with plain tokens from the first MiB of editor
text. No completion requests reach PostgreSQL; quoted identifiers and SQL context
are not inferred.

`editorstate.cpp` contains PgAdmMain's settings-based editor persistence. Normal
exit validates running jobs/queries and transaction closure, then saves tab SQL,
file/library paths, BOM, dirty state, target, widths, cursor and active tab. Startup
creates QueryTabs only, compares file-backed snapshots with disk to flag external
changes, and never creates PgSessions. Individual tab closure retains the existing
save/discard prompt. This is normal-exit restoration, not crash autosave.

Accepted private-session dispatches call `recordHistory`, including execution-to-file
and lazy-connection dispatch. Rejected submissions/connection failures create no
history entry. The newest 100 requests of at most 64,000 characters are stored in
the same isolated QSettings file; SQL is plain text, including any SQL literals.
The history dock filters SQL/context and opens copies in fresh tabs without replay.
History describes submissions, not completion outcomes. Results, native sessions,
transactions and connection-dialog passwords are excluded from editor persistence.

`ResultExporter` traverses the stored sections for CSV, XLSX and HTML. CSV is
UTF-8 with BOM, commas, quoted fields and doubled quotes. One section is a plain
table; multiple sections retain headings/status. XLSX is one overview sheet,
with explicit string cells to preserve exact values and prevent formula guessing.
Overlong Excel cells fail rather than being silently shortened.

`QueryTab::exportResults` sends an implicitly shared result snapshot to
`QtConcurrent`. The worker never reads a widget or mutable model.
`QtXlsxWriter` is the small Qt/libxlsxwriter bridge: sequential QStringList rows,
two fixed formats, pixel column widths and save. It owns the C handles, converts
UTF-8, validates Excel limits and uses libxlsxwriter's constant-memory mode. It
does not expose a spreadsheet object model or native handles to callers. A staged
QTemporaryDir gives libxlsxwriter its own output path, avoiding Windows file-handle
sharing conflicts. After workbook_close, QFile reads the staged workbook and
QSaveFile publishes its bytes.
Errors/abandoned exports preserve the destination. Embedded NUL characters are
rejected instead of silently cutting off C strings; CSV remains available.
Clipboard output contains both TSV text and escaped HTML.

CMake builds pinned libxlsxwriter and zlib sources statically. No Qt private
module is required. Tests inspect ZIP/XML with a Python-standard-library script,
independently of the writer. Python is required only with PGADM_BUILD_TESTS=ON.
Use libxlsxwriter's upstream build configuration without local source patches.
On MSVC, its PDB path and CMake's generated /Fd option can produce D9025 warnings.
These do not prevent compilation; no custom workaround is maintained for them.

## Catalog and object inspection

`CatalogTree` inherits profile/database/schema/relation context down its nodes.
Expanding a node requests only that branch from `CatalogService`. The service
keeps at most eight metadata connections and serialises requests per database.
Tree requests retain persistent model indexes; replies for deleted nodes are ignored.
Property queries use the same metadata queue, not a second connection pool.

`ObjectDetails::load` invalidates the previous selection and records a new
metadata request ID. Only a matching reply may fill the property table. The
detail pane loads only while visible. Database/schema properties are available;
relation and column properties include type, owner/default/identity information
where applicable. Estimated row counts are statistics, not exact counts.

Opening the definition tab starts a separate `pg_dump --schema-only` process for
one exactly quoted relation pattern. It includes no data, owner or privilege SQL.
The raw script is intended for psql and may contain psql directives; the query
editor does not support those directives. Referenced types, schemas and other
dependencies are not guaranteed to be included by a selective dump.

The child receives connection settings through its private process environment;
the password is never saved to a file or passed on the command line. It may use
the normal libpq password file when no in-memory password is present. A two-second
lock wait and 30-second overall timer limit waits; output is bounded at 8 MiB.
Changing selection or hiding the pane kills obsolete dumps. Errors and stderr
warnings are shown explicitly. Nothing in this view executes the displayed DDL.

## Database maintenance tasks

DatabaseJob::maintain runs VACUUM, ANALYZE and REINDEX through a dedicated PgSession,
not a query tab or explicit BEGIN. maintenanceSql quotes database/schema/relation
identifiers and builds the same SQL displayed in DatabaseTaskDialog. Database
targets and ordinary/partitioned tables/materialized views are supported. Relation
kind is carried in CatalogTree data rather than inferred from folder labels.
VACUUM ANALYZE and REINDEX CONCURRENTLY are defaults; VACUUM FULL is not offered.
The existing bounded log and elapsed timer cover these jobs as well. Native notices
remain visible, including skipped objects; success describes command completion.
Cancel uses libpq cancellation, waits for completion/disconnection, and does not
claim rollback of already finished maintenance steps. No user transaction is touched.

`PgAdmMain::openDatabaseTask` captures a profile and database from the actual
context menu. Server-level tasks select from the already loaded database list.
`DatabaseTaskDialog` is modeless: changing the tree selection cannot change its
captured target, and Start reviews that target before execution. A changed profile
invalidates the old task form. Drop requires the exact name, with standard system
and configured maintenance databases protected by `DatabaseJob::dropError`.

Before restore/drop, `releaseDatabase` refuses busy user queries, marks the target
node unavailable, closes only its metadata queue and deletes matching idle user
sessions. This rolls back their open transactions, as disclosed in the task form.
Query editor text and explicit target IDs survive. `CatalogTree::setDatabaseBusy`
also applies to nodes recreated during a refresh. `connectDatabase` rejects new
connections to a locked target. External clients are never terminated with FORCE.

`DatabaseJob` owns one actual maintenance process. It uses pg_dump, pg_restore,
psql, createdb or dropdb rather than sharing a user's query session. `PgTools`
supplies executable paths, private credential environments and escaped libpq
dbname arguments; this prevents names containing '=' from becoming connection
parameters. No shell or password command-line argument is involved.

Backup output goes to a private temporary file beside the chosen destination.
Only a successful dump starts `commitBackup`, which copies through QSaveFile on
QtConcurrent. An atomic flag cancels that copy without touching the old file.
The destructor waits for an active copy before deleting staging. Data never
travels through ResultModel, so query display limits do not apply to backups.

Restore detects archive headers rather than trusting a suffix. Custom/tar archives
use pg_restore with --single-transaction/--exit-on-error; clean and owner/ACL
options are explicit. Plain SQL uses psql without its rc file, with ON_ERROR_STOP
and --single-transaction. Arbitrary SQL/psql scripts may contain their own COMMIT
or connection commands; the transaction guarantee is for standard pg_dump output.
Cluster-wide roles are outside the scope of a single-database dump.

The task dialog uses QElapsedTimer for monotonic elapsed wall time, refreshed by
a 250 ms QTimer. Timing starts immediately before launching DatabaseJob and stops
on its finished signal, including the final backup file commit and cancellation.
The final duration remains visible for success and failure. It does not estimate
remaining time or a percentage of completed work.

Log text is decoded incrementally and bounded by line and character counts in the
dialog. Progress is indeterminate, never a fabricated percentage. Cancelling kills
the child or marks the background file copy for cancellation. CREATE/DROP may
already have committed, so every completion, including cancellation/failure,
refreshes the captured server's database list. Obsolete list replies are invalidated.
Task windows remain alive until the process/copy actually finishes; main-window
close is refused while a maintenance job is active.

## Server roles and database privileges

`PgAdmMain::openRoleManager` captures a server profile from the server/database
context menu. `RoleManager` owns a private PgSession for catalog reads and write
transactions; role changes never commit a query tab's transaction. Public
pg_roles metadata and membership names populate a baseline. `RoleSql` emits only
changed attributes and memberships, preserving options on unchanged memberships.
New roles use PostgreSQL's NOLOGIN defaults. Reserved pg_ roles are read-only.

Database ACL reads separate direct grants/grant options from effective privileges
obtained through PUBLIC, ownership or memberships. Applying changes only adjusts
direct CONNECT/CREATE/TEMPORARY grants, plus an explicitly selected owner change.
This does not grant privileges on tables or schemas. Switching editable tabs is
blocked while changes need applying/discarding. List/database changes and close
resolve unsaved forms; writes/rollback cannot be interrupted by closing.

Password input is transient and omitted from previews/settings/catalog reads.
PgSession::executeRoleChange queues role statements and the secret to its worker,
which prepares an explicit SCRAM-SHA-256 verifier with PQencryptPasswordConn before
sending SQL. The temporary UTF-8 password buffer is overwritten. This avoids
plaintext passwords in server statement logs; verifier handling still belongs to
the private role connection. BEGIN/COMMIT groups attributes, memberships and
password changes. A failed statement triggers an explicit ROLLBACK before editing
resumes. Server permissions remain authoritative.

The restore dialog's Roles button opens the same manager after a failed SQL
restore. A role name extracted from log text only prepopulates a form; it never
executes DDL. SQL dumps are not rewritten or stripped of ownership/ACL statements.
Missing roles must be reviewed/created before starting a fresh restore. Profiles
cannot be edited while their role window is open.

## Documentation and validation

`.github/workflows/build.yml` calls the same Windows build and integration scripts
as local development, then `tools/package-windows.ps1`. Visual Studio is discovered
using vswhere; Qt/CMake/Ninja locations can be overridden through environment
variables. Packaging stages explicit runtime files and MSVC DLLs, checks native
startup with isolated settings and a reduced PATH, then produces an Actions ZIP
artifact. It never copies entire build or integration directories. See `CI.md` for
the hosted-runner setup and remaining platform/distribution validation.

Header contracts describe public behaviour; implementation comments explain
nontrivial transitions and ownership. Avoid repeating the function name as prose.
Keep multiple statements on separate lines when that makes control flow clearer.

The current validated platform is Windows with Qt 6/MSVC and PostgreSQL 18.
`tests/run-integration.ps1` starts a disposable cluster on loopback, sets test
environment variables, and stops the server in `finally`. Unique vault keys
isolate credential tests. Test reports remain under the explicit build directory.
Windows fonts and local toolchain paths are current test/build assumptions, not
evidence of portability. A clean-machine distribution still needs validation.

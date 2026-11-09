param(
    [string]$PostgreSqlBin = (Join-Path $PSScriptRoot '..\third_party\postgresql18\pgsql\bin'),
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\build\Release')
)
$ErrorActionPreference = 'Stop'
$BuildDirectory = (Resolve-Path -LiteralPath $BuildDirectory).Path
$testExe = Join-Path $BuildDirectory 'tst_pgsession.exe'
if (!(Test-Path -LiteralPath $testExe)) { throw 'Build first: .\build-release.cmd -DPGADM_BUILD_TESTS=ON' }
if (!(Test-Path -LiteralPath (Join-Path $BuildDirectory 'platforms\qoffscreen.dll'))) {
    throw 'The offscreen test plugin is missing. Rebuild with PGADM_BUILD_TESTS=ON first.'
}
foreach ($tool in @('pg_dump', 'pg_restore', 'psql', 'createdb', 'dropdb')) {
    $toolPath = Join-Path $BuildDirectory ($tool + '.exe')
    if (!(Test-Path -LiteralPath $toolPath)) { throw "$tool is missing. Rebuild the application and tests first." }
    & $toolPath --version
    if ($LASTEXITCODE -ne 0) { throw "$tool cannot start. Rebuild to deploy its PostgreSQL runtime DLLs." }
}
$fixture = Join-Path $BuildDirectory ('integration-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$data = Join-Path $fixture 'data'
$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = $listener.LocalEndpoint.Port
$listener.Stop()
& (Join-Path $PostgreSqlBin 'initdb.exe') -D $data -U postgres --auth=trust --encoding=UTF8 --no-locale
if ($LASTEXITCODE -ne 0) { throw 'initdb failed' }
$hbaPath = Join-Path $data 'pg_hba.conf'
$hba = Get-Content -LiteralPath $hbaPath -Raw
Set-Content -LiteralPath $hbaPath -Value ("host all pgadm_test_auth,pgadm_test_role_password 127.0.0.1/32 scram-sha-256`n" + $hba) -Encoding ASCII
$started = $false
$oldPort = $env:PGADM_TEST_PORT
$oldPlatform = $env:QT_QPA_PLATFORM
try {
    & (Join-Path $PostgreSqlBin 'pg_ctl.exe') -D $data -l (Join-Path $fixture 'postgres.log') -o "-p $port -h 127.0.0.1" -w -t 20 start
    if ($LASTEXITCODE -ne 0) { throw 'Temporary PostgreSQL could not start' }
    $started = $true
    & (Join-Path $PostgreSqlBin 'psql.exe') -X -w -h 127.0.0.1 -p $port -U postgres -d postgres -v ON_ERROR_STOP=1 -c "CREATE ROLE pgadm_test_auth LOGIN PASSWORD 'temporary-test-only'"
    if ($LASTEXITCODE -ne 0) { throw 'Could not create temporary authentication fixture' }
    $env:PGADM_TEST_PORT = [string]$port
    $reportPath = Join-Path $fixture 'results.txt'
    & $testExe -o "$reportPath,txt"
    if (Test-Path -LiteralPath $reportPath) { Get-Content -LiteralPath $reportPath }
    if ($LASTEXITCODE -ne 0) { throw "Connection tests failed; see $fixture\results.txt" }
    $env:QT_QPA_PLATFORM = 'offscreen'
    $queryReport = Join-Path $fixture 'queries.txt'
    Push-Location $BuildDirectory
    try {
        & (Join-Path $BuildDirectory 'tst_queries.exe') -o "$queryReport,txt"
        $queryExit = $LASTEXITCODE
    } finally { Pop-Location }
    if (Test-Path -LiteralPath $queryReport) { Get-Content -LiteralPath $queryReport }
    if ($queryExit -ne 0) { throw "Query tests failed; see $queryReport" }
    $browserReport = Join-Path $fixture 'browser.txt'
    Push-Location $BuildDirectory
    try {
        & (Join-Path $BuildDirectory 'tst_browser.exe') -o "$browserReport,txt"
        $browserExit = $LASTEXITCODE
    } finally { Pop-Location }
    if (Test-Path -LiteralPath $browserReport) { Get-Content -LiteralPath $browserReport }
    if ($browserExit -ne 0) { throw "Browser tests failed; see $browserReport" }
} finally {
    $env:PGADM_TEST_PORT = $oldPort
    $env:QT_QPA_PLATFORM = $oldPlatform
    if ($started) {
        & (Join-Path $PostgreSqlBin 'pg_ctl.exe') -D $data -m fast -w -t 20 stop
        if ($LASTEXITCODE -ne 0) { Write-Warning "Temporary server shutdown failed: $data" }
    }
    Write-Output "Test files: $fixture"
}

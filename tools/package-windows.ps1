# Stage only application files, verify startup with isolated settings, then create a ZIP.
# Build output and smoke-test state stay outside the distributable application directory.
param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../build/Release'),
    [string]$QtRoot = 'C:/Qt/6.11.1/msvc2022_64',
    [ValidatePattern('^$|^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$')]
    [string]$Version = ''
)
$ErrorActionPreference = 'Stop'
$projectDirectory = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BuildDirectory = (Resolve-Path -LiteralPath $BuildDirectory).Path
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$staging = Join-Path $BuildDirectory ('packaging-' + [guid]::NewGuid().ToString('N'))
$application = Join-Path $staging 'alt-pg-admin'
New-Item -ItemType Directory -Path $application | Out-Null

$files = @(
    'pgadmin_qt.exe', 'pg_dump.exe', 'pg_restore.exe', 'psql.exe', 'createdb.exe', 'dropdb.exe',
    'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Network.dll', 'Qt6Widgets.dll', 'Qt6Svg.dll',
    'libpq.dll', 'libssl-3-x64.dll', 'libcrypto-3-x64.dll', 'libintl-9.dll', 'libiconv-2.dll',
    'libwinpthread-1.dll', 'liblz4.dll', 'libzstd.dll',
    'libxlsxwriter-LICENSE.txt', 'zlib-LICENSE.txt', 'QtKeychain-COPYING.txt',
    'Lucide-LICENSE.txt', 'server_license.txt', 'commandlinetools_3rd_party_licenses.txt'
)
foreach ($name in $files) {
    Copy-Item -LiteralPath (Join-Path $BuildDirectory $name) -Destination $application
}
foreach ($name in @('dxcompiler.dll', 'dxil.dll')) {
    $source = Join-Path $BuildDirectory $name
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $application }
}
New-Item -ItemType Directory -Path (Join-Path $application 'platforms') | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'platforms/qwindows.dll') -Destination (Join-Path $application 'platforms')
foreach ($folder in @('iconengines', 'imageformats', 'styles', 'tls', 'networkinformation', 'generic')) {
    $source = Join-Path $BuildDirectory $folder
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $application -Recurse }
}
foreach ($name in @('LICENSE', 'README.md')) {
    Copy-Item -LiteralPath (Join-Path $projectDirectory $name) -Destination $application
}

# Deploy the MSVC release runtime app-locally, so extraction needs no redistributable installer.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or !$vsRoot) { throw 'Visual Studio C++ runtime location could not be determined.' }
$redist = Get-ChildItem -LiteralPath (Join-Path $vsRoot 'VC/Redist/MSVC') -Directory |
    Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (!$redist) { throw 'The MSVC release runtime is missing.' }
$crt = Join-Path $redist.FullName 'x64/Microsoft.VC143.CRT'
Copy-Item -Path (Join-Path $crt '*.dll') -Destination $application

# Include the LGPL/GPL texts from the installed Qt SDK when provided by its distributor.
foreach ($folder in @((Join-Path $QtRoot 'LICENSES'), (Join-Path $QtRoot '../../Licenses'))) {
    if (Test-Path -LiteralPath $folder) {
        $licenseDestination = Join-Path $application 'Qt-licenses'
        New-Item -ItemType Directory -Path $licenseDestination -Force | Out-Null
        Get-ChildItem -LiteralPath $folder -File | Where-Object { $_.Name -match 'GPL|LGPL|EXCEPTION' } |
            Copy-Item -Destination $licenseDestination
    }
}

# Exercise the deployed native platform plugin with development paths removed.
$oldPath = $env:PATH
$oldPluginPath = $env:QT_PLUGIN_PATH
$oldPlatform = $env:QT_QPA_PLATFORM
$report = Join-Path $staging 'startup.txt'
try {
    $env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
    $env:QT_PLUGIN_PATH = $application
    $env:QT_QPA_PLATFORM = 'windows'
    $settingsPath = Join-Path $staging 'smoke-settings.ini'
    $arguments = @('--startup-check', '--startup-report', "`"$report`"", '--settings-file', "`"$settingsPath`"")
    $process = Start-Process -FilePath (Join-Path $application 'pgadmin_qt.exe') -ArgumentList $arguments `
        -WorkingDirectory $application -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(15000)) { $process.Kill(); throw 'The staged application did not finish its startup check.' }
    if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $report)) { throw 'The staged application could not start.' }
    if ($Version -and !((Get-Content -LiteralPath $report) -contains "Application version: $Version")) {
        throw 'The packaged executable version does not match the requested release version. Rebuild first.'
    }
    Get-Content -LiteralPath $report
} finally {
    $env:PATH = $oldPath
    $env:QT_PLUGIN_PATH = $oldPluginPath
    $env:QT_QPA_PLATFORM = $oldPlatform
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archiveName = if ($Version) { "alt-pg-admin-$Version-windows-x64.zip" } else { 'alt-pg-admin-windows-x64.zip' }
$archive = Join-Path $staging $archiveName
[System.IO.Compression.ZipFile]::CreateFromDirectory($application, $archive, [System.IO.Compression.CompressionLevel]::Optimal, $true)
$destination = Join-Path $projectDirectory 'dist'
New-Item -ItemType Directory -Path $destination -Force | Out-Null
Move-Item -LiteralPath $archive -Destination (Join-Path $destination $archiveName) -Force
Write-Output "Portable ZIP: $destination/$archiveName"

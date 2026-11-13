# Compile a private installer identity and exercise installation/reinstallation/removal.
# All files and settings use a fresh build fixture; production profiles and keys are untouched.
param(
    [Parameter(Mandatory = $true)][string]$SourceDirectory,
    [Parameter(Mandatory = $true)][string]$InnoCompiler,
    [Parameter(Mandatory = $true)][string]$Version
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$SourceDirectory = (Resolve-Path -LiteralPath $SourceDirectory).Path
$InnoCompiler = (Resolve-Path -LiteralPath $InnoCompiler).Path
$identity = [guid]::NewGuid().ToString('N')
$fixture = Join-Path $project "build/Release/installer-test-$identity"
$installed = [System.IO.Path]::GetFullPath((Join-Path $fixture 'Application'))
$buildRoot = [System.IO.Path]::GetFullPath((Join-Path $project 'build')) + [System.IO.Path]::DirectorySeparatorChar
if (!$installed.StartsWith($buildRoot, [System.StringComparison]::OrdinalIgnoreCase)) { throw 'Installer test path escapes the build directory.' }
New-Item -ItemType Directory -Path $fixture | Out-Null
$script = Join-Path $project 'packaging/windows/pgadmin-jr.iss'
& $InnoCompiler '/Q' "/DAppVersion=$Version" "/DSourceDirectory=$SourceDirectory" "/DOutputDirectory=$fixture" `
    "/DInstallerAppId=io.github.hdijkema.AltPgAdmin.Test.$identity" "/DInstallerName=pgadmin-jr Installer Test $identity" $script
if ($LASTEXITCODE -ne 0) { throw 'Private test installer compilation failed.' }
$setup = Join-Path $fixture "pgadmin-jr-$Version-windows-x64-setup.exe"

# Bound silent processes so a hidden installer prompt cannot hang local or hosted tests.
function Invoke-InstallerTestProcess([string]$Executable, [string[]]$Arguments) {
    $process = Start-Process -FilePath $Executable -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(30000)) { $process.Kill(); throw "Timed out: $Executable" }
    if ($process.ExitCode -ne 0) { throw "Exit code $($process.ExitCode): $Executable" }
}

$settings = Join-Path $fixture 'settings.ini'
$settingsContents = "[installer-test]`nmarker=$identity`n"
Set-Content -LiteralPath $settings -Value $settingsContents -Encoding ASCII -NoNewline
$settingsHash = (Get-FileHash -LiteralPath $settings).Hash
try {
    Invoke-InstallerTestProcess $setup @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOICONS', "/DIR=`"$installed`"", "/LOG=`"$fixture/install.log`"")
    $executable = Join-Path $installed 'pgadmin-jr.exe'
    if (!(Test-Path -LiteralPath $executable)) { throw 'The application was not installed.' }
    if ((Get-FileHash -LiteralPath $executable).Hash -ne (Get-FileHash -LiteralPath (Join-Path $SourceDirectory 'pgadmin-jr.exe')).Hash) {
        throw 'Installed application differs from the validated portable application.'
    }
    $report = Join-Path $fixture 'startup.txt'
    Invoke-InstallerTestProcess $executable @('--startup-check', '--settings-file', "`"$settings`"", '--startup-report', "`"$report`"")
    if (!((Get-Content -LiteralPath $report) -contains "Application version: $Version")) { throw 'Installed version check failed.' }
    if (!((Get-Content -LiteralPath $settings) -contains "marker=$identity")) { throw 'The separate settings marker was lost.' }
    # The application may normalise QSettings on startup; installer operations must preserve that state.
    $settingsHash = (Get-FileHash -LiteralPath $settings).Hash
    $query = Join-Path $installed 'user-query.sql'
    Set-Content -LiteralPath $query -Value "SELECT '$identity';" -Encoding ASCII
    $queryHash = (Get-FileHash -LiteralPath $query).Hash
    $svgLibrary = Join-Path $installed 'Qt6Svg.dll'
    Remove-Item -LiteralPath $svgLibrary
    Invoke-InstallerTestProcess $setup @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOICONS', "/DIR=`"$installed`"", "/LOG=`"$fixture/reinstall.log`"")
    if (!(Test-Path -LiteralPath $svgLibrary)) { throw 'Reinstallation did not restore an installed file.' }
    if ((Get-FileHash -LiteralPath $query).Hash -ne $queryHash) { throw 'Reinstallation changed an untracked query file.' }
} finally {
    $uninstaller = Join-Path $installed 'unins000.exe'
    if (Test-Path -LiteralPath $uninstaller) {
        Invoke-InstallerTestProcess $uninstaller @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$fixture/uninstall.log`"")
    }
}
if (Test-Path -LiteralPath (Join-Path $installed 'pgadmin-jr.exe')) { throw 'Uninstallation left the application executable.' }
if ((Get-FileHash -LiteralPath $query).Hash -ne $queryHash) { throw 'Uninstallation removed or changed the untracked query file.' }
if ((Get-FileHash -LiteralPath $settings).Hash -ne $settingsHash) { throw 'Installation or removal changed separate settings.' }
Write-Output "Installer install/reinstall/startup/uninstall checks passed: $fixture"

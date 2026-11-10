# Install the pinned compiler in a build-tools directory; no application data is read.
# Used on disposable GitHub runners, or explicitly for local installer development.
param([string]$Destination = (Join-Path $PSScriptRoot '../build/tools/inno-setup'))
$ErrorActionPreference = 'Stop'
$Destination = [System.IO.Path]::GetFullPath($Destination)
$compiler = Join-Path $Destination 'ISCC.exe'
if (Test-Path -LiteralPath $compiler) {
    Write-Output $compiler
    exit 0
}
$download = Join-Path $PSScriptRoot '../downloads/innosetup-7.1.1-x64.exe'
New-Item -ItemType Directory -Path (Split-Path $download) -Force | Out-Null
if (!(Test-Path -LiteralPath $download)) {
    Invoke-WebRequest -Uri 'https://github.com/jrsoftware/issrc/releases/download/is-7_1_1/innosetup-7.1.1-x64.exe' `
        -OutFile $download -UseBasicParsing
}
$expected = '8a5a6711c3766a6e5fb74f280ba0e3551e719ce94538d901fdbd1f8f9920b3f0'
if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw 'Inno Setup download checksum does not match the pinned upstream release.'
}
$arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/CURRENTUSER', '/NOICONS', "/DIR=`"$Destination`"")
$setup = Start-Process -FilePath ([System.IO.Path]::GetFullPath($download)) -ArgumentList $arguments `
    -WindowStyle Hidden -Wait -PassThru
if ($setup.ExitCode -ne 0 -or !(Test-Path -LiteralPath $compiler)) { throw 'Inno Setup compiler installation failed.' }
Write-Output $compiler

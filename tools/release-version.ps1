# Resolve a release only for an explicit vX.Y.Z push or manual version input.
# Return build metadata without writing tags, contacting GitHub or publishing files.
param(
    [string]$EventName = '',
    [string]$RefType = '',
    [string]$RefName = '',
    [string]$RequestedVersion = ''
)
$ErrorActionPreference = 'Stop'
$version = ''
if ($EventName -eq 'push' -and $RefType -eq 'tag' -and $RefName.StartsWith('v')) {
    $version = $RefName.Substring(1)
    if (!$version) { throw 'A release tag must have the form vX.Y.Z.' }
}
if ($EventName -eq 'workflow_dispatch' -and $RequestedVersion) {
    $version = $RequestedVersion
    if ($version.StartsWith('v')) { $version = $version.Substring(1) }
    if (!$version) { throw 'Enter a version with the form X.Y.Z.' }
    if ($RefType -eq 'tag' -and $RefName -ne "v$version") {
        throw 'The requested version must match the selected tag.'
    }
}
if ($version -and $version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
    throw 'Use a numeric release version such as 0.2.0, without leading zeroes.'
}
[pscustomobject]@{
    Version = $version
    Publish = [bool]$version
    Tag = if ($version) { "v$version" } else { '' }
    Archive = if ($version) { "alt-pg-admin-$version-windows-x64.zip" } else { 'alt-pg-admin-windows-x64.zip' }
}

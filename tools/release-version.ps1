# Resolve a release for an explicit X.Y.Z/vX.Y.Z tag push or manual version input.
# Return build metadata without writing tags, contacting GitHub or publishing files.
param(
    [string]$EventName = '',
    [string]$RefType = '',
    [string]$RefName = '',
    [string]$RequestedVersion = ''
)
$ErrorActionPreference = 'Stop'
$version = ''
$tag = ''
if ($EventName -eq 'push' -and $RefType -eq 'tag') {
    $tag = $RefName
    $version = $tag
    if ($version.StartsWith('v')) { $version = $version.Substring(1) }
    if (!$version) { throw 'A release tag must have the form X.Y.Z or vX.Y.Z.' }
}
if ($EventName -eq 'workflow_dispatch' -and $RequestedVersion) {
    $version = $RequestedVersion
    if ($version.StartsWith('v')) { $version = $version.Substring(1) }
    if (!$version) { throw 'Enter a version with the form X.Y.Z.' }
    $tag = "v$version"
    if ($RefType -eq 'tag') {
        if ($RefName -ne $version -and $RefName -ne "v$version") {
            throw 'The requested version must match the selected tag.'
        }
        $tag = $RefName
    }
}
if ($version -and $version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
    throw 'Use a numeric release version such as 0.2.0, without leading zeroes.'
}
[pscustomobject]@{
    Version = $version
    Publish = [bool]$version
    Tag = $tag
    Archive = if ($version) { "pgadmin-jr-$version-windows-x64.zip" } else { 'pgadmin-jr-windows-x64.zip' }
}

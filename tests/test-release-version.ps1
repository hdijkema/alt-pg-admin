# Verify release eligibility and version parsing without touching Git tags or GitHub.
$ErrorActionPreference = 'Stop'
$resolve = Join-Path $PSScriptRoot '../tools/release-version.ps1'
$release = & $resolve -EventName push -RefType tag -RefName v1.2.3
if (!$release.Publish -or $release.Version -ne '1.2.3' -or $release.Tag -ne 'v1.2.3') {
    throw 'Tag resolution failed.'
}
$release = & $resolve -EventName workflow_dispatch -RequestedVersion '0.2.0'
if (!$release.Publish -or $release.Archive -ne 'pgadmin-jr-0.2.0-windows-x64.zip') {
    throw 'Manual release resolution failed.'
}
$release = & $resolve -EventName workflow_dispatch -RequestedVersion 'v0.2.0'
if ($release.Version -ne '0.2.0') { throw 'The optional v prefix was not normalised.' }
foreach ($tag in @('0.3.2', 'v0.3.2')) {
    $release = & $resolve -EventName push -RefType tag -RefName $tag
    if (!$release.Publish -or $release.Version -ne '0.3.2' -or $release.Tag -ne $tag) {
        throw "Tag spelling was not preserved: $tag"
    }
    $release = & $resolve -EventName workflow_dispatch -RefType tag -RefName $tag -RequestedVersion '0.3.2'
    if (!$release.Publish -or $release.Tag -ne $tag) { throw 'Manual tag resolution failed.' }
}
foreach ($event in @('pull_request', 'push')) {
    $release = & $resolve -EventName $event -RefType branch -RefName main -RequestedVersion '1.2.3'
    if ($release.Publish -or $release.Version) { throw 'A branch build must not publish.' }
}
$release = & $resolve -EventName workflow_dispatch
if ($release.Publish) { throw 'A manual build without a version must not publish.' }
foreach ($bad in @('01.2.3', 'v01.2.3', '1.2', 'v1.2', '1.2.3-rc1', 'v1.2.3-rc1', 'v', 'v1.2.3;echo unsafe')) {
    $rejected = $false
    try { $null = & $resolve -EventName push -RefType tag -RefName $bad }
    catch { $rejected = $true }
    if (!$rejected) { throw "Accepted invalid version $bad" }
}
$rejected = $false
try { $null = & $resolve -EventName workflow_dispatch -RefType tag -RefName v1.0.0 -RequestedVersion 2.0.0 }
catch { $rejected = $true }
if (!$rejected) { throw 'A manual release must match the selected tag.' }
Write-Output 'Release version checks passed.'

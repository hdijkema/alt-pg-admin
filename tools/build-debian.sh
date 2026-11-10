#!/usr/bin/env bash
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_dir"
# Dependencies are installed by the caller (CI container or local administrator).
. /etc/os-release
if [[ ${ID:-} != debian || ! ${VERSION_ID:-} =~ ^(12|13)$ ]]; then
    echo 'This script packages Debian 12 or 13.' >&2
    exit 1
fi
build_dir="build/debian${VERSION_ID}"
cmake -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr -DPGADM_BUILD_TESTS=ON \
    "-DPGADM_DEBIAN_VERSION=$VERSION_ID" "-DPGADM_RELEASE_VERSION=${PGADM_RELEASE_VERSION:-}" "$@"
cmake --build "$build_dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
# No running database, credentials or desktop keyring are required by this subset.
QT_QPA_PLATFORM=offscreen ctest --test-dir "$build_dir" --output-on-failure -R '^(pgsession|queries)$'
QT_QPA_PLATFORM=offscreen "$build_dir/tst_browser" translations savedQueriesAndWidths \
    editorSearchAndCompletion editorTabsRestore profileDialogKeepsSecretsSeparate \
    databaseJobValidation roleSqlValidation maintenanceSqlValidation
for language in nl en de; do
    QT_QPA_PLATFORM=offscreen "$build_dir/pgadmin_qt" --language "$language" \
        --settings-file "$repo_dir/$build_dir/smoke-$language.ini" --startup-check \
        --startup-report "$repo_dir/$build_dir/startup-$language.txt"
done
mkdir -p dist
cpack --config "$build_dir/CPackConfig.cmake" -G DEB -B dist

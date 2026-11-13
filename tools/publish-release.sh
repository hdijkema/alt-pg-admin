#!/usr/bin/env bash
# Publish only complete, matching artifacts from the current workflow run.
set -euo pipefail
package_dir=${1:-release}
version=${RELEASE_VERSION:?RELEASE_VERSION is required}
release_tag=${RELEASE_TAG:?RELEASE_TAG is required}
if [[ ! $version =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ || ( $release_tag != "v$version" && $release_tag != "$version" ) ]]; then
    echo 'The release tag must match the numeric package version.' >&2
    exit 1
fi
assets=(
    "$package_dir/pgadmin-jr-$version-windows-x64.zip"
    "$package_dir/pgadmin-jr-$version-windows-x64-setup.exe"
    "$package_dir/pgadmin-jr_$version-debian12_amd64.deb"
    "$package_dir/pgadmin-jr_$version-debian13_amd64.deb"
    "$package_dir/pgadmin-jr_$version-debian12_arm64.deb"
    "$package_dir/pgadmin-jr_$version-debian13_arm64.deb"
    "$package_dir/pgadmin-jr-$version-linux-x86_64.flatpak"
    "$package_dir/pgadmin-jr-$version-macos-arm64.dmg"
)
for asset in "${assets[@]}"; do
    if [[ ! -s $asset ]]; then
        echo "Missing or empty release asset: $asset" >&2
        exit 1
    fi
done
if git show-ref --verify --quiet "refs/tags/$release_tag"; then
    tag_commit=$(git rev-parse "$release_tag^{commit}")
    if [[ $tag_commit != "$(git rev-parse HEAD)" ]]; then
        echo 'The existing release tag points to a different commit.' >&2
        exit 1
    fi
fi
if existing_assets=$(gh release view "$release_tag" --json assets --jq '.assets[].name'); then
    missing_assets=()
    for asset in "${assets[@]}"; do
        name=${asset##*/}
        found=false
        while IFS= read -r existing; do
            if [[ $existing == "$name" ]]; then found=true; break; fi
        done <<< "$existing_assets"
        if [[ $found == false ]]; then missing_assets+=("$asset"); fi
    done
    if (( ${#missing_assets[@]} )); then
        gh release upload "$release_tag" "${missing_assets[@]}"
    else
        echo "All package assets are already attached to $release_tag."
    fi
else
    gh release create "$release_tag" "${assets[@]}" \
        --target "$(git rev-parse HEAD)" --title "pgadmin-jr $release_tag" --generate-notes
fi

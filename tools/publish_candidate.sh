#!/usr/bin/env bash
set -euo pipefail

run_id=${1:-}
repo=${GITHUB_REPOSITORY:?Run this command in GitHub Actions}
if [[ ! "$run_id" =~ ^[0-9]+$ ]]; then
    echo "Supply the release candidate's Actions run ID" >&2
    exit 1
fi

run=$(gh api "repos/$repo/actions/runs/$run_id")
if ! jq -e --arg repo "$repo" '
    .path == ".github/workflows/release.yml" and
    .event == "workflow_dispatch" and
    .head_branch == "main" and
    .head_repository.full_name == $repo and
    .conclusion == "success"
' >/dev/null <<<"$run"; then
    echo "Expected a successful release workflow run from main" >&2
    exit 1
fi

commit=$(jq -r .head_sha <<<"$run")
attempt=$(jq -r .run_attempt <<<"$run")
git merge-base --is-ancestor "$commit" HEAD
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
gh run download "$run_id" --repo "$repo" \
    --name "palm-wifi-sdio-candidate-$run_id-$attempt" --dir "$work/candidate"
mapfile -t metadata < "$work/candidate/candidate.txt"
version=${metadata[1]:-}
if [[ ${#metadata[@]} != 2 || ${metadata[0]} != "$commit" ||
      ! "$version" =~ ^v[0-9]+\.[0-9]+(\.[0-9]+)?$ ]]; then
    echo "Candidate metadata does not match the successful build" >&2
    exit 1
fi

asset="PalmOne-E2-WPA2-$version.zip"
(
    cd "$work/candidate"
    sha256sum "$asset" | cmp - "$asset.sha256"
)
if git show-ref --verify --quiet "refs/tags/$version" ||
        gh release view "$version" --repo "$repo" >/dev/null 2>&1; then
    echo "A tag or release for $version already exists" >&2
    exit 1
fi

git show "$commit:doc/release-notes.md" > "$work/notes.md"
printf '\n[Candidate build](https://github.com/%s/actions/runs/%s)\n' "$repo" "$run_id" >> "$work/notes.md"
gh release create "$version" --repo "$repo" --target "$commit" --draft \
    --title "Palm Wi-Fi SDIO $version" --notes-file "$work/notes.md" \
    "$work/candidate/$asset" "$work/candidate/$asset.sha256"
gh release download "$version" --repo "$repo" --pattern "$asset" \
    --pattern "$asset.sha256" --dir "$work/uploaded"
cmp "$work/candidate/$asset" "$work/uploaded/$asset"
cmp "$work/candidate/$asset.sha256" "$work/uploaded/$asset.sha256"
gh release edit "$version" --repo "$repo" --draft=false

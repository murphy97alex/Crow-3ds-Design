#!/usr/bin/env bash
# Downloads Linux x86_64 builds of bannertool and makerom into ./tools/bin.
# Looks up the latest GitHub release of each candidate repo and takes the first
# asset whose zip contains a working binary.
set -euo pipefail

DEST="$(pwd)/tools/bin"
mkdir -p "$DEST"
WORK="$(mktemp -d)"

api() {
  local auth=()
  [ -n "${GITHUB_TOKEN:-}" ] && auth=(-H "Authorization: Bearer ${GITHUB_TOKEN}")
  curl -fsSL "${auth[@]}" -H "Accept: application/vnd.github+json" "$1"
}

# fetch_tool <binary-name> <asset-url-regex> <repo> [<repo> ...]
fetch_tool() {
  local name="$1" pattern="$2"; shift 2
  for repo in "$@"; do
    echo "::group::Looking for $name in $repo"
    local urls
    urls="$(api "https://api.github.com/repos/$repo/releases?per_page=30" \
      | jq -r '.[].assets[].browser_download_url' 2>/dev/null \
      | grep -Ei '\.zip$' | grep -Ei "$pattern" | grep -Eiv 'win|mac|osx|darwin|arm64|aarch64' || true)"
    echo "$urls"
    for url in $urls; do
      local d="$WORK/$name-$RANDOM"
      mkdir -p "$d"
      curl -fsSL -o "$d/a.zip" "$url" || continue
      unzip -qo "$d/a.zip" -d "$d/x" || continue
      local bin
      while IFS= read -r bin; do
        chmod +x "$bin"
        if file "$bin" | grep -q 'ELF 64-bit.*x86-64'; then
          cp "$bin" "$DEST/$name"
          echo "Using $url -> $bin"
          echo "::endgroup::"
          return 0
        fi
      done < <(find "$d/x" -type f -name "$name")
    done
    echo "::endgroup::"
  done
  echo "ERROR: could not find a Linux x86_64 $name" >&2
  return 1
}

# makerom >= 0.19.0 Linux builds need glibc 2.38; the devkitPro image (Debian) has 2.36.
fetch_tool makerom    'makerom-v0\.18\.4-ubuntu' 3DSGuy/Project_CTR
fetch_tool bannertool 'bannertool' Epicpkmn11/bannertool diasurgical/bannertool Steveice10/bannertool

ls -la "$DEST"

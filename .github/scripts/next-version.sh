#!/usr/bin/env bash
# Computes the next SemVer from Conventional Commits since the last vX.Y.Z tag.
#   BREAKING CHANGE / type!:  -> major
#   feat:                     -> minor
#   anything else             -> patch
# Prints the version without the leading "v". See ADR 0002.
set -euo pipefail

last_tag=$(git describe --tags --abbrev=0 --match 'v[0-9]*.[0-9]*.[0-9]*' 2>/dev/null || true)
if [[ -z "$last_tag" ]]; then
  base="0.0.0"
  range="HEAD"
else
  base="${last_tag#v}"
  range="${last_tag}..HEAD"
fi

IFS=. read -r major minor patch <<<"$base"

log=$(git log --format='%s%n%b%n--END--' "$range")

if grep -qE '^(BREAKING CHANGE|BREAKING-CHANGE):|^[a-z]+(\([^)]*\))?!:' <<<"$log"; then
  bump=major
elif grep -qE '^feat(\([^)]*\))?:' <<<"$log"; then
  bump=minor
else
  bump=patch
fi

# Before 1.0.0, breaking changes bump minor (SemVer 0.x convention).
if [[ "$bump" == major && "$major" == 0 ]]; then
  bump=minor
fi

case "$bump" in
  major) major=$((major + 1)); minor=0; patch=0 ;;
  minor) minor=$((minor + 1)); patch=0 ;;
  patch) patch=$((patch + 1)) ;;
esac

echo "${major}.${minor}.${patch}"

#!/usr/bin/env bash
# prepare-release.sh — create a clean release branch and open a PR to master.
#
# SYNOPSIS
#   tools/prepare-release.sh <version>
#
# DESCRIPTION
#   This script automates the develop → master release path. It:
#     1. Verifies the caller is on develop and that develop is up to date
#        with origin/develop (no accidental releases from a stale state).
#     2. Creates a release/<version> branch from the current HEAD of develop.
#     3. Removes every path listed in .github/release-strip.txt (internal
#        docs, design files, spec files, dev tooling, AI config, etc.).
#     4. Commits the removal with a clear "release prep" commit message.
#     5. Pushes the release branch to origin.
#     6. Opens a pull request from release/<version> into master via the
#        GitHub CLI (gh).
#
#   Because branch protection on master requires PRs and CI to pass, the
#   actual merge is always a human decision — this script just does the
#   mechanical preparation so the developer doesn't have to remember which
#   files to delete.
#
# REQUIREMENTS
#   - git
#   - gh (GitHub CLI), authenticated: gh auth login
#   - Must be run from the repository root
#   - STRIP_LIST (.github/release-strip.txt) must be present
#
# EXAMPLE
#   tools/prepare-release.sh v1.0.0
#
# AFTER MERGING THE PR
#   Tag the resulting master commit:
#     git fetch origin
#     git tag -a <version> origin/master -m "Release <version>"
#     git push origin <version>
#   The release.yml workflow will then build the binary and create a
#   GitHub Release automatically.

set -euo pipefail

# ── Argument validation ────────────────────────────────────────────────────

if [[ $# -ne 1 ]]; then
    echo "Usage: tools/prepare-release.sh <version>" >&2
    echo "  Example: tools/prepare-release.sh v1.0.0" >&2
    exit 1
fi

VERSION="$1"

# Require a 'v' prefix so tags are consistent (v1.0.0, v1.2.3-rc1, etc.).
if [[ ! "${VERSION}" =~ ^v[0-9]+\.[0-9]+\.[0-9] ]]; then
    echo "ERROR: version must start with 'v' followed by semver (e.g. v1.0.0)." >&2
    exit 1
fi

# ── Locate the strip list ──────────────────────────────────────────────────

REPO_ROOT="$(git rev-parse --show-toplevel)"
STRIP_LIST="${REPO_ROOT}/.github/release-strip.txt"

if [[ ! -f "${STRIP_LIST}" ]]; then
    echo "ERROR: strip list not found: ${STRIP_LIST}" >&2
    echo "  Expected .github/release-strip.txt to exist in the repository root." >&2
    exit 1
fi

# ── Dependency checks ─────────────────────────────────────────────────────

if ! command -v gh &>/dev/null; then
    echo "ERROR: 'gh' (GitHub CLI) is not installed or not in PATH." >&2
    echo "  Install it: https://cli.github.com/" >&2
    exit 1
fi

if ! gh auth status &>/dev/null; then
    echo "ERROR: GitHub CLI is not authenticated. Run: gh auth login" >&2
    exit 1
fi

# ── Branch guard ──────────────────────────────────────────────────────────

CURRENT_BRANCH="$(git rev-parse --abbrev-ref HEAD)"

if [[ "${CURRENT_BRANCH}" != "develop" ]]; then
    echo "ERROR: releases must be prepared from the 'develop' branch." >&2
    echo "  Current branch: ${CURRENT_BRANCH}" >&2
    echo "  Switch to develop: git checkout develop" >&2
    exit 1
fi

# Check for uncommitted changes — a dirty working tree means something was
# forgotten and the release would capture an incomplete state.
if ! git diff --quiet HEAD; then
    echo "ERROR: working tree has uncommitted changes." >&2
    echo "  Commit or stash them before preparing a release." >&2
    git diff --stat HEAD >&2
    exit 1
fi

# Ensure develop is up to date with origin so we don't release a stale commit.
echo "Fetching origin to verify develop is current..."
git fetch origin develop

LOCAL_SHA="$(git rev-parse HEAD)"
REMOTE_SHA="$(git rev-parse origin/develop)"

if [[ "${LOCAL_SHA}" != "${REMOTE_SHA}" ]]; then
    echo "ERROR: local develop (${LOCAL_SHA:0:7}) differs from origin/develop (${REMOTE_SHA:0:7})." >&2
    echo "  Pull the latest changes: git pull --ff-only origin develop" >&2
    exit 1
fi

# ── Check for duplicate release branch ────────────────────────────────────

RELEASE_BRANCH="release/${VERSION}"

if git rev-parse --verify "refs/heads/${RELEASE_BRANCH}" &>/dev/null; then
    echo "ERROR: branch '${RELEASE_BRANCH}' already exists locally." >&2
    echo "  Delete it first if you want to re-prepare: git branch -D ${RELEASE_BRANCH}" >&2
    exit 1
fi

if git ls-remote --exit-code origin "refs/heads/${RELEASE_BRANCH}" &>/dev/null; then
    echo "ERROR: branch '${RELEASE_BRANCH}' already exists on origin." >&2
    exit 1
fi

# ── Create release branch ─────────────────────────────────────────────────

echo "Creating branch '${RELEASE_BRANCH}' from develop (${LOCAL_SHA:0:7})..."
git checkout -b "${RELEASE_BRANCH}"

# ── Strip develop-only files ──────────────────────────────────────────────

echo "Reading strip list: ${STRIP_LIST}"

REMOVED_COUNT=0

while IFS= read -r line || [[ -n "${line}" ]]; do
    # Skip blank lines and comments.
    [[ -z "${line}" || "${line}" =~ ^[[:space:]]*# ]] && continue

    # Normalise: strip surrounding whitespace, then trailing slash.
    path="${line#"${line%%[![:space:]]*}"}"   # ltrim
    path="${path%"${path##*[![:space:]]}"}"   # rtrim
    path="${path%/}"                           # remove trailing slash

    if [[ -z "${path}" ]]; then
        continue
    fi

    FULL_PATH="${REPO_ROOT}/${path}"

    if [[ ! -e "${FULL_PATH}" ]]; then
        # Path doesn't exist — either already absent or was never created in
        # this repo state. Skip silently; this is expected for some entries
        # that are listed as a safety net (e.g. .claude/ which is gitignored).
        continue
    fi

    # Only remove paths that are tracked by git. git-ignored files are
    # irrelevant to the branch and rm-ing them would silently delete local
    # developer state on the release branch.
    if git ls-files --error-unmatch "${path}" &>/dev/null 2>&1 || \
       [[ -n "$(git ls-files "${path}")" ]]; then
        echo "  Removing: ${path}"
        git rm -r --quiet "${path}"
        (( REMOVED_COUNT++ )) || true
    else
        echo "  Skipping (not tracked): ${path}"
    fi

done < "${STRIP_LIST}"

# ── Commit the cleanup ────────────────────────────────────────────────────

if [[ "${REMOVED_COUNT}" -eq 0 ]]; then
    echo "No tracked develop-only files found to remove."
    echo "Nothing to commit — the release branch is identical to develop."
    echo ""
    echo "If this is unexpected, verify .github/release-strip.txt is correct."
    # Clean up the branch we just created since there's nothing to do.
    git checkout develop
    git branch -D "${RELEASE_BRANCH}"
    exit 0
fi

echo "Committing removal of ${REMOVED_COUNT} develop-only path(s)..."

git commit -m "$(cat <<EOF
chore: strip develop-only files for release ${VERSION}

Removes internal documentation, design specs, developer tooling, and
AI assistant configuration files that are not part of the production
release. These files live on the develop branch and are stripped via
.github/release-strip.txt before merging to master.

Removed paths:
$(git diff --cached --name-only --diff-filter=D | sed 's/^/  - /')
EOF
)"

# ── Push and open PR ──────────────────────────────────────────────────────

echo "Pushing '${RELEASE_BRANCH}' to origin..."
git push -u origin "${RELEASE_BRANCH}"

echo "Opening PR: ${RELEASE_BRANCH} → master..."
PR_URL="$(gh pr create \
    --base master \
    --head "${RELEASE_BRANCH}" \
    --title "Release ${VERSION}" \
    --body "$(cat <<EOF
## Release ${VERSION}

This PR merges \`develop\` into \`master\` for the **${VERSION}** release.

### What was stripped

Develop-only files have been removed from this branch per \`.github/release-strip.txt\`:

$(git diff origin/master...HEAD --name-only --diff-filter=D 2>/dev/null | sed 's/^/- /' || echo "- (see commit for full list)")

### Pre-merge checklist

- [ ] CI passes (build + full test suite)
- [ ] \`CHANGELOG.md\` / release notes are up to date (if applicable)
- [ ] Version constant in source matches ${VERSION} (if applicable)

### Post-merge steps

After merging, tag the resulting master commit to trigger the release build:

\`\`\`bash
git fetch origin
git tag -a ${VERSION} origin/master -m "Release ${VERSION}"
git push origin ${VERSION}
\`\`\`

The \`release.yml\` workflow will then build the binary and publish a GitHub Release automatically.
EOF
    )"
)"

echo ""
echo "-----------------------------------------------------------"
echo "Release branch prepared successfully."
echo ""
echo "  Branch : ${RELEASE_BRANCH}"
echo "  PR     : ${PR_URL}"
echo ""
echo "Next steps:"
echo "  1. Review the PR and confirm the strip list is correct."
echo "  2. Wait for CI to pass."
echo "  3. Merge the PR on GitHub."
echo "  4. Tag the master commit to publish the release:"
echo "       git fetch origin"
echo "       git tag -a ${VERSION} origin/master -m \"Release ${VERSION}\""
echo "       git push origin ${VERSION}"
echo "-----------------------------------------------------------"

# Return to develop so the developer isn't left on the release branch.
git checkout develop

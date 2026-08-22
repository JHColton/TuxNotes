#!/usr/bin/env bash
# Build a portable x86_64 AppImage inside a fedora:40 container.
# Baseline: glibc 2.39 — runs on Fedora 40+, Ubuntu 24.04+, Debian 13+.
#
# Prereq: podman (or docker) with network access.
set -euo pipefail

cd "$(dirname "$0")/.."
mkdir -p dist

RUNNER=podman
command -v podman >/dev/null || RUNNER=docker

echo "==> building in ${RUNNER} container (fedora:40)..."
${RUNNER} run --rm -v "$PWD":/src:Z -w /src fedora:40 bash packaging/appimage-inside.sh

echo "==> done:"
ls -la dist/

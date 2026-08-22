#!/usr/bin/env bash
# Build a local RPM for Fedora.
# Prereq (once): sudo dnf install -y rpm-build qt6-qtbase-devel gcc-c++ cmake desktop-file-utils
set -euo pipefail

cd "$(dirname "$0")/.."
VERSION=$(grep -oP '(?<=project\(tuxnotes VERSION )\S+' CMakeLists.txt)
TARBALL="tuxnotes-${VERSION}.tar.gz"

echo "==> version: ${VERSION}"

rm -rf ~/rpmbuild/SOURCES/tuxnotes-${VERSION}
mkdir -p ~/rpmbuild/{SOURCES,SPECS,RPMS,SRPMS,BUILD,BUILDROOT}
# Package the WORKING TREE (not HEAD) so local edits are included.
tar --transform "s,^,tuxnotes-${VERSION}/," \
    --exclude=build --exclude=.git --exclude=dist --exclude=.cache \
    -czf ~/rpmbuild/SOURCES/${TARBALL} .
cp packaging/tuxnotes.spec ~/rpmbuild/SPECS/

echo "==> building (output RPM lands in ~/rpmbuild/RPMS/x86_64/)"
rpmbuild -bb ~/rpmbuild/SPECS/tuxnotes.spec

echo "==> done:"
ls -la ~/rpmbuild/RPMS/x86_64/

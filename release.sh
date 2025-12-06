set -xe

VERSION=$1
git tag release-$VERSION
git push origin release-$VERSION
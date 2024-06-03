#!/bin/sh
die () {
  echo "generate: error: $*" 1>&2
  exit 1
}
[ -f VERSION ] || die "could not find 'VERSION' file"
[ -d .git ] || die "could not find '.git' directory"
[ -f makefile ] || die "could not find 'makefile'"
VERSION="`cat VERSION`"
GITID="`git show|head -1|awk '{print $2}'`"
cat <<EOF
#define VERSION "$VERSION"
#define GITID "$GITID"
EOF

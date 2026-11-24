#!/bin/sh
DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
export PLUME_ROOT="$DIR"
exec "$DIR/plume-session"

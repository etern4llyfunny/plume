#!/bin/sh
DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
export PLUME_ROOT="$DIR"
# Isolate D-Bus activation from the host desktop: otherwise applications
# started in nested Plume can reuse host instances and keep their host theme.
if ! command -v dbus-run-session >/dev/null 2>&1; then
  echo 'plume: dbus-run-session is required for a nested session' >&2
  exit 1
fi
export PLUME_PRIVATE_DBUS=1
exec dbus-run-session -- "$DIR/plume-session"

#!/bin/sh
export XDG_DATA_DIRS="$HOME/plume/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export GTK_THEME="Plume"
exec labwc -C "$HOME/plume/config/labwc"

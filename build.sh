#!/usr/bin/env sh
set -xe

mkdir -p build

CFLAGS="-Wall -Wextra -pedantic -ggdb -std=c11"
CLIBS=""

gcc $CFLAGS -o build/bus-watcher src/main.c src/utils.c src/json.c $CLIBS

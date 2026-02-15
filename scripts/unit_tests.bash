#!/usr/bin/env bash

# assumes PWD being parent directory ... TODO polish later

set -e

scripts/build.bash \
    --target exec_path_args_unit_tests \
    --target some_cli_app

input=(
    build/tests/unit/exec_path_args_unit_tests
    --no-intro=true
    --no-version=true
)

if [[ "$1" == "--gdb" ]]; then
    shift
    gdb --args "${input[@]}" "$@"
else
    time "${input[@]}" "$@"
fi

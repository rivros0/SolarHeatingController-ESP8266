#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
bin="$(mktemp /tmp/solar-tests.XXXXXX)"
trap 'rm -f "$bin"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -g -Ilib/ControlCore test/native/test_control.cpp -o "$bin"
"$bin"

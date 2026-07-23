#!/bin/sh

set -eu

ROOT_DIRECTORY=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$ROOT_DIRECTORY"

CXX=${CXX:-c++}
if ! command -v "$CXX" >/dev/null 2>&1; then
	echo "Error: C++ compiler '$CXX' was not found." >&2
	exit 1
fi

# GCC 9 and older Clang releases use the pre-standardisation c++2a spelling.
STANDARD_FLAG=-std=c++20
if ! printf 'int main() { return 0; }\n' |
	"$CXX" "$STANDARD_FLAG" -x c++ -fsyntax-only - >/dev/null 2>&1
then
	STANDARD_FLAG=-std=c++2a
	if ! printf 'int main() { return 0; }\n' |
		"$CXX" "$STANDARD_FLAG" -x c++ -fsyntax-only - >/dev/null 2>&1
	then
		echo "Error: '$CXX' does not support C++20." >&2
		exit 1
	fi
	echo "Using the compiler's legacy -std=c++2a spelling for C++20."
fi

set --
CARRIAGE_RETURN=$(printf '\r')
while IFS= read -r SOURCE || [ -n "$SOURCE" ]; do
	SOURCE=${SOURCE%"$CARRIAGE_RETURN"}
	case "$SOURCE" in
		""|\#*) continue ;;
	esac
	set -- "$@" "$SOURCE"
done < cli-sources.txt

mkdir -p cli-build

THREAD_FLAG=-pthread
if [ "$(uname -s)" = "Darwin" ]; then
	THREAD_FLAG=
fi

"$CXX" "$STANDARD_FLAG" -O3 -DNDEBUG ${THREAD_FLAG:+"$THREAD_FLAG"} -I. \
	"$@" PollingCli.cpp -o cli-build/polling-cli

echo "Built $ROOT_DIRECTORY/cli-build/polling-cli"

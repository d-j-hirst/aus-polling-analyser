#!/bin/sh

set -eu

ROOT_DIRECTORY=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$ROOT_DIRECTORY"

CXX=${CXX:-c++}
if ! command -v "$CXX" >/dev/null 2>&1; then
	echo "Error: C++ compiler '$CXX' was not found." >&2
	exit 1
fi

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
fi

TEST_DIRECTORY="$ROOT_DIRECTORY/cli-build/tests"
OBJECT_DIRECTORY="$TEST_DIRECTORY/obj"
rm -rf "$TEST_DIRECTORY"
mkdir -p "$OBJECT_DIRECTORY"

THREAD_FLAG=-pthread
if [ "$(uname -s)" = "Darwin" ]; then
	THREAD_FLAG=
fi

CARRIAGE_RETURN=$(printf '\r')
while IFS= read -r SOURCE || [ -n "$SOURCE" ]; do
	SOURCE=${SOURCE%"$CARRIAGE_RETURN"}
	case "$SOURCE" in
		""|\#*) continue ;;
	esac
	OBJECT="$OBJECT_DIRECTORY/$(basename "${SOURCE%.cpp}").o"
	"$CXX" "$STANDARD_FLAG" -O1 -I"$ROOT_DIRECTORY" \
		${THREAD_FLAG:+"$THREAD_FLAG"} \
		-c "$ROOT_DIRECTORY/$SOURCE" -o "$OBJECT"
done < cli-sources.txt

while IFS= read -r TEST || [ -n "$TEST" ]; do
	TEST=${TEST%"$CARRIAGE_RETURN"}
	case "$TEST" in
		""|\#*) continue ;;
	esac
	"$CXX" "$STANDARD_FLAG" -O1 -I"$ROOT_DIRECTORY" \
		${THREAD_FLAG:+"$THREAD_FLAG"} \
		"$ROOT_DIRECTORY/tests/$TEST.cpp" "$OBJECT_DIRECTORY"/*.o \
		-o "$TEST_DIRECTORY/$TEST"
done < portable-test-sources.txt

run_test()
{
	TEST_NAME=$1
	shift
	echo "Running $TEST_NAME"
	"$TEST_DIRECTORY/$TEST_NAME" "$@"
}

run_test DateTests
run_test ForecastSpecificationTests
run_test LiveDataTests
run_test MacroTargetResolverTests
run_test RandomGeneratorTests
run_test TerminalMacroFeedbackTests
run_test WorkspacePathsTests "$ROOT_DIRECTORY"
run_test CoreReportSummaryTests
run_test ForecastSpecificationTests \
	"$ROOT_DIRECTORY" "$ROOT_DIRECTORY"/forecasts/*/forecast.json
run_test ForecastSpecificationProjectAdapterTests \
	"$ROOT_DIRECTORY" "$ROOT_DIRECTORY"/forecasts/*/forecast.json
run_test CliDependencyBoundaryTests \
	"$ROOT_DIRECTORY" "$ROOT_DIRECTORY"/forecasts/*/forecast.json

echo "All portable tests passed."

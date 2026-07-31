#!/bin/bash
# Build the EDITOR target and run the Automation specs headless in the dev-5.5 container.
#
# The automation specs are EditorContext, so they need UnrealEditor-Cmd and the *Editor* target --
# the game target this project ships cannot run them at all.
#
# PASS/FAIL DISCIPLINE, and why it is not just "grep for Success":
# a crash mid-suite TRUNCATES the run. Tests that already passed still print Success, the ones that
# never ran print nothing at all, and the log ends looking like a clean pass with a smaller number
# in it. So this checks, in order: the container's exit code, the presence of any fatal marker, that
# every Test Started got a Test Completed, that no result was {Fail}, and that the count is not
# zero (which is what a filter typo or a boot crash looks like). Same rules as
# Scripts/RunTests.Common.ps1 on Windows -- both had to learn them the hard way.
#
# Usage: run_tests_linux.sh [test-filter]     (default: the whole MetaHumanLocalLLM suite)
set -o pipefail
FILTER="${1:-MetaHumanLocalLLM}"
PROJ="${PROJ:-/home/ci/cook-clean}"
LOG="${LOG:-/home/ci/tests_linux.log}"

docker run --rm --user root -v "$PROJ":/proj --entrypoint bash ghcr.io/epicgames/unreal-engine:dev-5.5 -c "
    mkdir -p /proj/Content && chown -R ue4:ue4 /proj
    su ue4 -c '/home/ue4/UnrealEngine/Engine/Build/BatchFiles/Linux/Build.sh MetaHumanLocalLLMEditor Linux Development -project=/proj/MetaHumanLocalLLM.uproject' || exit 1
    echo ===RUNNING_TESTS===
    su ue4 -c '/home/ue4/UnrealEngine/Engine/Binaries/Linux/UnrealEditor-Cmd /proj/MetaHumanLocalLLM.uproject -ExecCmds=\"Automation RunTests $FILTER; Quit\" -unattended -nullrhi -nosplash -nosound -stdout -NoLogTimes'
  " > "$LOG" 2>&1
RUN_EXIT=$?

STARTED=$(grep -c 'Test Started\.' "$LOG")
COMPLETED=$(grep -c 'Test Completed\. Result=' "$LOG")
FAILED=$(grep -c 'Test Completed\. Result={Fail' "$LOG")
CRASH=$(grep -cE 'appError called|LowLevelFatalError|Fatal error:|Assertion failed' "$LOG")

echo "filter=$FILTER exit=$RUN_EXIT started=$STARTED completed=$COMPLETED failed=$FAILED crash_markers=$CRASH"

if ! grep -q '===RUNNING_TESTS===' "$LOG"; then
  echo "BUILD_FAILED - the editor target did not compile:"; grep -E 'error:|Error:' "$LOG" | head -30; exit 1
fi
[ "$RUN_EXIT" -eq 0 ]        || { echo "TESTS_FAILED - nonzero exit $RUN_EXIT"; exit 1; }
[ "$CRASH" -eq 0 ]           || { echo "TESTS_FAILED - crash markers present"; grep -E 'appError called|LowLevelFatalError|Fatal error:|Assertion failed' "$LOG" | head -5; exit 1; }
[ "$COMPLETED" -gt 0 ]       || { echo "TESTS_FAILED - no results (filter matched nothing, or a boot crash)"; exit 1; }
[ "$STARTED" -eq "$COMPLETED" ] || { echo "TESTS_FAILED - truncated: $STARTED started, $COMPLETED completed"; exit 1; }
[ "$FAILED" -eq 0 ]          || { echo "TESTS_FAILED - $FAILED failing:"; grep 'Result={Fail' "$LOG" | head -20; exit 1; }
echo "TESTS_OK - $COMPLETED passed"

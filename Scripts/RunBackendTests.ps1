param(
    [string]$EngineDir = "C:\Program Files\Epic Games\UE_5.8"
)

. (Join-Path $PSScriptRoot "RunTests.Common.ps1")

# Take the LAST object the helper emitted, not the whole result. PowerShell returns everything a
# function wrote to its success stream, so one unredirected command inside it (an unpiped
# Build.bat, once) turns this capture into an Object[] -- and `exit <array>` exits 0 silently,
# even `exit @(1)`. That is how a suite that printed "FAILED - 1 of 2 test(s) failed" still
# reported green to CI. The helper's `return` is always the last object out, so [-1] recovers it;
# `-as [int]` rejects leftover build chatter instead of throwing the way a cast would. The Count
# check has to come first: `$null -as [int]` is 0, so an empty result would otherwise exit 0 too.
$Result = @(Invoke-MetaHumanLLMTests -TestFilter "MetaHumanLocalLLM.Backend" -LogName "BackendTests" -EngineDir $EngineDir)
$ExitCode = if ($Result.Count -gt 0) { $Result[-1] -as [int] } else { $null }
if ($null -eq $ExitCode) {
    Write-Error "Invoke-MetaHumanLLMTests returned no usable exit code - treating as failure."
    exit 1
}
exit $ExitCode

param(
    [string]$EngineDir = "C:\Program Files\Epic Games\UE_5.8"
)

. (Join-Path $PSScriptRoot "RunTests.Common.ps1")

$ExitCode = Invoke-MetaHumanLLMTests -TestFilter "MetaHumanLocalLLM.Backend" -LogName "BackendTests" -EngineDir $EngineDir
exit $ExitCode

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformMisc.h"

// Env-var helpers, shared.
//
// These were previously duplicated in an anonymous namespace in three .cpp files. An anonymous
// namespace normally guarantees internal linkage per translation unit -- but Unreal's unity build
// concatenates several .cpp files INTO one translation unit, at which point the anonymous
// namespaces merge and the duplicate definitions collide ("error C2084: function already has a
// body"). Linux/clang grouped these files apart and never noticed; Windows/MSVC grouped them
// together and failed. Whether it breaks is a property of file ordering, not of the code, so the
// only durable fix is to define them once.
namespace CICompanionEnv
{
	inline FString Get(const TCHAR* Name, const FString& Default)
	{
		const FString Value = FPlatformMisc::GetEnvironmentVariable(Name);
		return Value.IsEmpty() ? Default : Value;
	}

	// Explicitly OFF. Absent means "not configured", which callers treat as the default rather
	// than as disabled.
	inline bool IsDisabled(const TCHAR* Name)
	{
		const FString V = FPlatformMisc::GetEnvironmentVariable(Name);
		return V == TEXT("0") || V.Equals(TEXT("false"), ESearchCase::IgnoreCase);
	}

	inline bool IsEnabled(const TCHAR* Name)
	{
		const FString V = FPlatformMisc::GetEnvironmentVariable(Name);
		return V == TEXT("1") || V.Equals(TEXT("true"), ESearchCase::IgnoreCase);
	}
}

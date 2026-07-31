#pragma once

#include "CoreMinimal.h"
#include "Compat/CICompanionCompat.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

// Skeletal-mesh and animation-curve calls that differ between UE 5.5 and UE 5.8.
//
// Header-only and inline: every function here is either a one-line forward or a two-branch #if, so a
// .cpp would add a translation unit without adding clarity.
//
// SCOPE, HONESTLY STATED
// Most of what UCompanionFaceComponent does needs NO shim. Verified against both installs:
//   USkeletalMeshComponent::GetSkeletalMeshAsset()                      identical
//   USkeletalMesh::FindMorphTarget(FName) const                         identical
//     5.5 SkeletalMesh.h:2648   5.8 SkeletalMesh.h:2757
//   USkeletalMeshComponent::SetMorphTarget(FName, float, bool = true)   identical
//     5.5 SkinnedMeshComponent.h:1215   5.8 :1347
// Those are wrapped anyway, so that there is exactly ONE place to change if a future engine moves
// them, and so call sites read consistently. They are cheap and they cost nothing at runtime.
//
// Exactly one call in this header is a genuine polyfill: TryGetCurveValue.

struct FCISkeletalMeshCompat
{
	static USkeletalMesh* GetSkeletalMeshAsset(const USkeletalMeshComponent* Component)
	{
		return Component ? Component->GetSkeletalMeshAsset() : nullptr;
	}

	// Whether the mesh actually carries this morph target.
	//
	// Worth routing through a helper because the failure mode is invisible: SetMorphTarget on a name
	// the mesh does not have silently does nothing, and the character just stares back with a still
	// face. UCompanionFaceComponent::ResolveCurvesAgainstMesh already logs loudly for this reason.
	static bool HasMorphTarget(const USkeletalMeshComponent* Component, FName MorphTargetName)
	{
		const USkeletalMesh* Asset = GetSkeletalMeshAsset(Component);
		return Asset && Asset->FindMorphTarget(MorphTargetName) != nullptr;
	}

	static void SetMorphTarget(USkeletalMeshComponent* Component, FName MorphTargetName, float Value)
	{
		if (Component)
		{
			Component->SetMorphTarget(MorphTargetName, Value);
		}
	}

	// THE POLYFILL. Reads back an evaluated animation curve value.
	//
	//   5.8: USkeletalMeshComponent::GetCurveValue(FName, float DefaultValue, float& Value) const
	//        exists on the component  [SkeletalMeshComponent.h:664, alongside GetAnimCurves() at :667]
	//   5.5: it does NOT exist on the component at all. The only accessor lives on UAnimInstance:
	//        UAnimInstance::GetCurveValue(FName, float& OutValue) const  [AnimInstance.h:1174]
	//
	// Both paths read the same evaluated curve set, so the shim is behaviour-preserving rather than
	// an approximation. Returns false when the curve is not present, leaving OutValue untouched --
	// callers get "absent" and "zero" as distinguishable answers, which matters for a face rig where
	// a missing curve is a rig-authoring bug and a zero curve is a resting mouth.
	//
	// NOTE ON THE 5.8 DEPRECATION: 5.8 marks the AnimCurves member UE_DEPRECATED(5.6) ("please use
	// GetAnimCurves or GetCurveValue instead. We will move to private in the future",
	// SkeletalMeshComponent.h:470). So neither direct member access nor the new component accessor is
	// safe to write unconditionally across both engines -- which is precisely why this shim exists
	// rather than a call site picking one.
	static bool TryGetCurveValue(const USkeletalMeshComponent* Component, FName CurveName, float& OutValue)
	{
		if (!Component)
		{
			return false;
		}

#if CI_WITH_SKELMESHCOMP_CURVE_ACCESSORS
		// 5.8+: ask the component. It owns the evaluated curve set directly.
		float Value = 0.0f;
		if (Component->GetCurveValue(CurveName, 0.0f, Value))
		{
			OutValue = Value;
			return true;
		}
		return false;
#else
		// 5.5: go through the anim instance, the only place the accessor exists.
		if (const UAnimInstance* AnimInstance = Component->GetAnimInstance())
		{
			float Value = 0.0f;
			if (AnimInstance->GetCurveValue(CurveName, Value))
			{
				OutValue = Value;
				return true;
			}
		}
		return false;
#endif
	}

	// Convenience form for call sites that genuinely do not care whether a curve is absent or zero.
	static float GetCurveValueOrDefault(const USkeletalMeshComponent* Component, FName CurveName, float DefaultValue = 0.0f)
	{
		float Value = DefaultValue;
		return TryGetCurveValue(Component, CurveName, Value) ? Value : DefaultValue;
	}
};

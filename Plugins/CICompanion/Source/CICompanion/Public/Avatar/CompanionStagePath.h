#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class UCompanionStageSettings;

// A camera move through a list of points: a centripetal Catmull-Rom curve (no cusps or
// overshoot, C1 through every key), sampled by ARC LENGTH so equal times cover equal distances,
// and timed by an S-curve whose velocity rises with smootherstep over AccelFrac of the move,
// cruises, and falls over DecelFrac (velocity and acceleration are continuous, zero at both ends).
// The look-at follows its own curve at the same parameter, so neither position nor aim can jump
// at a key. Plain C++ with no world, so the automation specs drive it directly.
struct CICOMPANION_API FCompanionStagePath
{
	// LookAts must match Points one for one (or be empty: the camera then looks at Points' next key).
	// Screens are per-key composition targets, interpolated by distance progress; may be empty.
	void Build(const TArray<FVector>& InPoints, const TArray<FVector>& InLookAts, const TArray<FVector2D>& InScreens,
		float InSeconds, float InAccelFrac, float InDecelFrac);

	// Re-times the same geometry.
	void SetSeconds(float InSeconds) { Seconds = FMath::Max(InSeconds, 0.0f); }

	bool IsMoving() const { return Points.Num() > 1 && Seconds > 0.0f && Length > KINDA_SMALL_NUMBER; }
	float GetSeconds() const { return Seconds; }
	float GetLength() const { return Length; }
	float GetAccelFrac() const { return AccelFrac; }
	float GetDecelFrac() const { return DecelFrac; }
	const TArray<FVector>& GetPoints() const { return Points; }

	// Fraction of the path's length covered at Time: 0 at the start, 1 from Seconds on, never
	// decreasing.
	float ProgressAt(float Time) const;
	FVector PositionAt(float Time) const;
	FVector LookAtAt(float Time) const;
	FVector2D ScreenAt(float Time) const;
	FVector PositionAtProgress(float Progress) const;
	FVector LookAtAtProgress(float Progress) const;

	// Normalised distance covered by the S-curve at U in [0, 1]. AccelFrac + DecelFrac above 1 is
	// scaled down to 1 (no cruise). Peak speed = mean speed / (1 - AccelFrac/2 - DecelFrac/2).
	static float SCurve(float U, float AccelFrac, float DecelFrac);

	// Centripetal Catmull-Rom (alpha 0.5) through P at parameter U in [0, P.Num() - 1]; the end
	// segments use mirrored phantom points, so the curve starts and ends on its first and last key.
	static FVector CentripetalCatmullRom(const TArray<FVector>& P, float U);

private:
	float ParamAtDistance(float Distance) const;

	TArray<FVector> Points;
	TArray<FVector> LookAts;
	TArray<FVector2D> Screens;
	TArray<float> TableParam;
	TArray<float> TableLength;
	TArray<float> KeyProgress;
	float Seconds = 0.0f;
	float AccelFrac = 0.3f;
	float DecelFrac = 0.5f;
	float Length = 0.0f;
	bool bConstantLookAt = true;
};

// A framing the camera holds: where it stands, what it aims at, and how the frame is composed.
struct CICOMPANION_API FCompanionStageFraming
{
	bool bValid = false;
	FVector Location = FVector::ZeroVector;
	FVector LookAt = FVector::ZeroVector;
	float FocalMm = 40.4f;
	FVector2D FilmbackMm = FVector2D(36.0, 20.25);
	float Aperture = 0.0f;
	// Resident shots: hold her eyes (LookAt) at Screen with sensor shift. Otherwise ShiftMm is fixed.
	bool bHoldScreen = false;
	FVector2D Screen = FVector2D(0.5, 0.5);
	FVector2D ShiftMm = FVector2D::ZeroVector;
};

// Where the resident is: her home mark, her home facing, and the height of her eyes.
struct CICOMPANION_API FCompanionStageSubject
{
	FVector Mark = FVector::ZeroVector;
	float FacingYaw = 180.0f;
	float EyeZ = 158.1f;
};

// Everything the director needs to play the opening: the lobby framing, the move, the home
// framing, and when each beat falls. Built once at BeginPlay from the settings (or the built-in
// fallback) and the measured resident.
struct CICOMPANION_API FCompanionStagePlan
{
	bool bFromConfig = false;
	// Config shots keep the camera level and compose with sensor shift; the fallback aims with pitch.
	bool bLevel = false;
	FVector Aim = FVector::ZeroVector;
	float StatureCm = 170.0f;

	FCompanionStageFraming Lobby;
	FCompanionStageFraming Home;
	// The punch-in for long replies (the CLOSE shot); invalid on the built-in path.
	FCompanionStageFraming Close;
	FCompanionStagePath Opening;
	// The lens of the move.
	float FocalMm = 40.4f;
	FVector2D FilmbackMm = FVector2D(36.0, 20.25);
	float Aperture = 0.0f;

	// Seconds the lobby is held after a session starts, before the move.
	float HoldSeconds = 3.0f;
	float MoveSeconds = 11.0f;
	// The lobby framing is not the move's first key, so starting the move is a camera cut.
	bool bCutIntoMove = false;

	// Beats, in seconds from the start of the move.
	float GreetingAt = -1.0f;
	FString GreetingLine;
	float LookUpAt = 0.0f;
	float TurnStart = 0.0f;
	float TurnEnd = 0.0f;
	// She starts this many degrees from her home yaw and turns back over [TurnStart, TurnEnd].
	float WorkTurnDegrees = 0.0f;
	float SettleAt = 0.0f;
	float SpeakAt = 0.0f;

	// MHLLM_OPENING_SHOT=0: seconds after the hold.
	float StaticGreetingAt = -1.0f;
	float StaticSpeakAt = 0.35f;

	// Measured on the planned move (sampled at 240 Hz), for the log and the specs.
	float PeakSpeedCmPerSecond = 0.0f;
	float MinTimeToContactSeconds = 0.0f;
	float MinEyeDistanceCm = 0.0f;

	// Decisions worth a log line (a cap below the comfort floor, a clamped start, ...).
	TArray<FString> Notes;
};

namespace CompanionStage
{
	// The shots and beats from the settings, solved for this resident; falls back to
	// BuildFallbackPlan when the settings have no opening. Env answers MHLLM_FLY_SECONDS and
	// MHLLM_OPENING_DELAY.
	CICOMPANION_API FCompanionStagePlan BuildPlan(const UCompanionStageSettings* Settings, const FCompanionStageSubject& Subject,
		TFunctionRef<FString(const TCHAR*)> Env, bool bPortrait = false);

	// The built-in opening (the path the director flew before the stage layout existed), resolved
	// against the subject, timed by MHLLM_FLY_SECONDS (default 11 s).
	CICOMPANION_API FCompanionStagePlan BuildFallbackPlan(const FCompanionStageSubject& Subject, TFunctionRef<FString(const TCHAR*)> Env);

	// The built-in path's six keys: positions and look-ats, relative to the subject's mark.
	CICOMPANION_API void FallbackKeys(const FVector& Subject, float EyeZ, TArray<FVector>& OutPositions, TArray<FVector>& OutLookAts);

	// Degrees between her yaw and her home yaw MoveTime seconds into the move (WorkTurnDegrees
	// before the turn, 0 after it, smoothstepped between).
	CICOMPANION_API float ResidentYawOffsetAt(const FCompanionStagePlan& Plan, float MoveTime);

	// Level camera at Location, yawed at Aim: the sensor shift in mm (Blender's sign, + moves the
	// window right or up) that puts Aim at Screen (0..1 from the left, 0..1 from the top).
	CICOMPANION_API FVector2D SensorShiftFor(const FVector& Location, const FVector& Aim, const FVector2D& Screen,
		float FocalMm, const FVector2D& FilmbackMm);

	// The rotation a framing's camera takes: level (yaw only) or aimed straight at LookAt.
	CICOMPANION_API FRotator AimRotation(const FVector& Location, const FVector& LookAt, bool bLevel);

	// Rotation-only "breathing" for a held shot, Seconds into the run: two incommensurate slow sines
	// per axis (0.07-0.14 Hz), yaw within +-AmplitudeDegrees and pitch within +-0.6 x that, never
	// faster than 0.6 x AmplitudeDegrees a second. Roll is always 0, and so is all of it at amplitude 0.
	CICOMPANION_API FRotator BreathOffset(float Seconds, float AmplitudeDegrees);
}

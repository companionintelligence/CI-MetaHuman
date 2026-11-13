#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CompanionStageSettings.generated.h"

// Which space a stage shot's keys are written in.
UENUM()
enum class ECompanionStageFrame : uint8
{
	// Relative to the resident: range and bearing from her eye anchor, height above her eyes. These
	// shots follow her mark (StageMark.Home) and scale with her measured stature.
	Resident,
	// Set-relative world positions: the frieze truck and the archive cutaways.
	World
};

// One key of a stage shot.
USTRUCT()
struct CICOMPANION_API FCompanionStageKey
{
	GENERATED_BODY()

	// Resident frame: horizontal distance from her eye anchor, written for the reference stature and
	// scaled by hers.
	UPROPERTY(VisibleAnywhere, Category = "Stage")
	float RangeCm = 0.0f;

	// Resident frame: degrees from her home facing toward her LEFT.
	UPROPERTY(VisibleAnywhere, Category = "Stage")
	float ThetaDeg = 0.0f;

	// Resident frame: height above her eyes, scaled like RangeCm.
	UPROPERTY(VisibleAnywhere, Category = "Stage")
	float DzCm = 0.0f;

	// Resident frame: where her eyes sit in the frame, 0..1 from the left and 0..1 from the top. The
	// camera stays level and holds them there with sensor shift.
	UPROPERTY(VisibleAnywhere, Category = "Stage")
	FVector2D Screen = FVector2D(0.5, 0.5);

	// World frame: where the camera is.
	UPROPERTY(VisibleAnywhere, Category = "Stage")
	FVector Location = FVector::ZeroVector;

	// World frame: what it looks at.
	UPROPERTY(VisibleAnywhere, Category = "Stage")
	FVector LookAt = FVector::ZeroVector;
};

// One shot of the stage design: a lens and one key (a held framing) or several (a move).
USTRUCT()
struct CICOMPANION_API FCompanionStageShot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Stage")
	FName Name;

	UPROPERTY(VisibleAnywhere, Category = "Stage")
	ECompanionStageFrame Frame = ECompanionStageFrame::Resident;

	// Focal length on FilmbackMm. The field of view is derived, never set.
	UPROPERTY(VisibleAnywhere, Category = "Stage")
	float FocalMm = 50.0f;

	// Sensor width and height in mm: 36 x 20.25 (16:9), or 20.25 x 36 for a portrait shot.
	UPROPERTY(VisibleAnywhere, Category = "Stage")
	FVector2D FilmbackMm = FVector2D(36.0, 20.25);

	UPROPERTY(VisibleAnywhere, Category = "Stage")
	float Aperture = 4.0f;

	// The move's duration at the reference stature; 0 for a held shot. The director re-derives the
	// opening's duration from its comfort budget for the measured resident.
	UPROPERTY(VisibleAnywhere, Category = "Stage")
	float Seconds = 0.0f;

	// S-curve easing: velocity rises with smootherstep over this fraction of the move, cruises,
	// and falls over DecelFrac.
	UPROPERTY(VisibleAnywhere, Category = "Stage")
	float AccelFrac = 0.3f;

	UPROPERTY(VisibleAnywhere, Category = "Stage")
	float DecelFrac = 0.5f;

	// Resident frame: no key may stand farther than this from her eyes (the opening starts inside
	// the glass for any stature); 0 = no limit.
	UPROPERTY(VisibleAnywhere, Category = "Stage")
	float MaxRangeCm = 0.0f;

	// World frame: sensor shift in mm, Blender's sign (+ moves the window right or up).
	UPROPERTY(VisibleAnywhere, Category = "Stage")
	FVector2D ShiftMm = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Stage")
	TArray<FCompanionStageKey> Keys;
};

// The stage camera's shots and beat timings, read by UCompanionStageDirector.
//
// GENERATED, not hand-edited: Deployment/stage_layout.py writes this class's section of
// Config/DefaultGame.ini from Deployment/stage_layout.json, the same file author_diorama.py builds
// the map from, so one file drives both the set and the camera. It is config rather than map data
// because the map is authored headlessly in a content-only project that can neither carry a
// Sequencer asset nor reference plugin classes (Docs/HANDOFF.md, Lesson 6).
//
// When there are no shots (no section, or an empty one), the director falls back to its built-in
// opening path, so a project without the layout still has a camera.
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "CI Companion Stage"))
class CICOMPANION_API UCompanionStageSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UCompanionStageSettings();

	// Which layout file (and revision) wrote this section.
	UPROPERTY(Config, VisibleAnywhere, Category = "Layout")
	FString LayoutSource;

	// The stature the resident-frame ranges are written for; other statures scale them.
	UPROPERTY(Config, VisibleAnywhere, Category = "Resident")
	float ReferenceStatureCm = 170.0f;

	// Her eye anchor stands this far in front of her mark, toward the lens (scaled with stature).
	UPROPERTY(Config, VisibleAnywhere, Category = "Resident")
	float EyeForwardCm = 9.0f;

	// Eye height as a fraction of stature, for turning a measured eye height into a stature.
	UPROPERTY(Config, VisibleAnywhere, Category = "Resident")
	float EyeFraction = 0.93f;

	// Her home facing in world yaw, used when the map has no StageMark.Home TargetPoint.
	UPROPERTY(Config, VisibleAnywhere, Category = "Resident")
	float HomeFacingYaw = 180.0f;

	UPROPERTY(Config, VisibleAnywhere, Category = "Shots")
	FName LobbyShot = TEXT("VITRINE");

	UPROPERTY(Config, VisibleAnywhere, Category = "Shots")
	FName OpeningShot = TEXT("ARRIVAL");

	UPROPERTY(Config, VisibleAnywhere, Category = "Shots")
	FName HomeShot = TEXT("HOME");

	UPROPERTY(Config, VisibleAnywhere, Category = "Shots")
	FName CloseShot = TEXT("CLOSE");

	// Seconds the lobby still is held after a session starts, before the cut into the move: after
	// the level loads for a kiosk or a native window, after the first viewer connects for a stream
	// (UCompanionStageDirector, EStageTrigger). The visitor's own key, click or Esc in the lobby
	// shortens it to 0.3 s. MHLLM_OPENING_DELAY overrides it.
	UPROPERTY(Config, VisibleAnywhere, Category = "Beats")
	float HoldSeconds = 3.0f;

	// The distance salutation, spoken while the camera is still walking in. Empty = none.
	UPROPERTY(Config, VisibleAnywhere, Category = "Beats")
	FString GreetingLine;

	// Seconds into the move; negative = no greeting.
	UPROPERTY(Config, VisibleAnywhere, Category = "Beats")
	float GreetingAtSeconds = -1.0f;

	// She looks up when the camera comes this close to her eyes (scaled with stature)...
	UPROPERTY(Config, VisibleAnywhere, Category = "Beats")
	float LookUpDistanceCm = 350.0f;

	// ...and her body turns this long after the look-up...
	UPROPERTY(Config, VisibleAnywhere, Category = "Beats")
	float TurnDelaySeconds = 0.2f;

	// ...over this long...
	UPROPERTY(Config, VisibleAnywhere, Category = "Beats")
	float TurnSeconds = 0.95f;

	// ...back from her at-work yaw, this many degrees from her home yaw. Never more than 30.
	UPROPERTY(Config, VisibleAnywhere, Category = "Beats")
	float WorkTurnDegrees = 30.0f;

	// The move has settled once the camera is slower than this.
	UPROPERTY(Config, VisibleAnywhere, Category = "Beats")
	float SettleSpeedCmPerSecond = 2.0f;

	// The turn-yield line (MHLLM_OPENING_LINE) comes this long after the settle.
	UPROPERTY(Config, VisibleAnywhere, Category = "Beats")
	float SpeakAfterSettleSeconds = 0.6f;

	// MHLLM_OPENING_SHOT=0 (reduced motion): the lobby still cuts straight to HOME when the hold
	// ends (no move, no breathing); the greeting and the line come this long after that cut.
	UPROPERTY(Config, VisibleAnywhere, Category = "Beats")
	float ReducedMotionGreetingSeconds = 0.8f;

	UPROPERTY(Config, VisibleAnywhere, Category = "Beats")
	float ReducedMotionSpeakSeconds = 2.0f;

	// Live framing (design 3.1 "Reply"): a reply at least this long punches in to CLOSE at its first
	// sentence boundary 1.5 s or more in, once per reply...
	UPROPERTY(Config, VisibleAnywhere, Category = "Live")
	float CloseMinReplySeconds = 6.0f;

	// ...and comes back to HOME on the visitor's first key or click, or this long after the reply.
	UPROPERTY(Config, VisibleAnywhere, Category = "Live")
	float CloseReturnSeconds = 4.0f;

	// Automatic cuts (the punch-in) are at least this far apart (design 3.2). A cut the visitor
	// asks for with a key, click or Esc is never delayed.
	UPROPERTY(Config, VisibleAnywhere, Category = "Live")
	float CutSpacingSeconds = 4.0f;

	// Rotation-only breathing on the held shots (the lobby still, HOME, CLOSE): yaw within this many
	// degrees, pitch within 0.6 x it (CompanionStage::BreathOffset). 0, MHLLM_STAGE_BREATHING=0 or
	// the reduced-motion path turn it off.
	UPROPERTY(Config, VisibleAnywhere, Category = "Live")
	float BreathingDegrees = 0.15f;

	// Comfort budget of the opening move (design 6.3): peak speed at the reference stature, scaled
	// by min(1, stature / reference)...
	UPROPERTY(Config, VisibleAnywhere, Category = "Comfort")
	float MaxSpeedMetersPerSecond = 0.8f;

	// ...the duration's clamp (MHLLM_FLY_SECONDS replaces the upper bound, never below the comfort
	// floor)...
	UPROPERTY(Config, VisibleAnywhere, Category = "Comfort")
	float MinMoveSeconds = 5.0f;

	UPROPERTY(Config, VisibleAnywhere, Category = "Comfort")
	float MaxMoveSeconds = 8.0f;

	// ...and the shortest time to contact while she is approached.
	UPROPERTY(Config, VisibleAnywhere, Category = "Comfort")
	float MinTimeToContactSeconds = 4.2f;

	UPROPERTY(Config, VisibleAnywhere, Category = "Shots")
	TArray<FCompanionStageShot> Shots;

	const FCompanionStageShot* FindShot(FName Name) const;

	// Name's portrait variant (NAME_PORTRAIT) when bPortrait and one exists, else Name.
	const FCompanionStageShot* FindShotFor(FName Name, bool bPortrait) const;

	// True when the shots needed for the data-driven opening are present: the opening move with at
	// least two keys and a home framing. Otherwise the director uses its built-in path.
	bool HasOpening() const;
};

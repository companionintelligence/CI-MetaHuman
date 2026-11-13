#include "Misc/AutomationTest.h"
#include "Avatar/CompanionStageDirector.h"
#include "Avatar/CompanionStagePath.h"
#include "Avatar/CompanionSetState.h"
#include "Avatar/CompanionRuntimeSubsystem.h"
#include "Avatar/TapeReelSpinner.h"
#include "Settings/CompanionStageSettings.h"
#include "Engine/World.h"
#include "GameFramework/DefaultPawn.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

BEGIN_DEFINE_SPEC(FCompanionStageSpec, "MetaHumanLocalLLM.Frontend.CompanionStage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FCompanionStageSpec)

namespace
{
	// Prefixed: the unity build merges anonymous namespaces across .cpp files.
	TFunction<FString(const TCHAR*)> StageSpecLookup(const TMap<FString, FString>& Env)
	{
		return [Env](const TCHAR* Name) -> FString
		{
			const FString* Found = Env.Find(FString(Name));
			return Found ? *Found : FString();
		};
	}

	// A subject on the 1963 room's home mark, (650, -132), facing the lens down -X, eyes at 0.93 x stature. The plan is
	// solved relative to her mark and facing, so the 1979 room's mark (650, -500, facing 170) gives the same move.
	FCompanionStageSubject StageSpecSubject(float StatureCm)
	{
		FCompanionStageSubject Subject;
		Subject.Mark = FVector(650.0, -132.0, 0.0);
		Subject.FacingYaw = 180.0f;
		Subject.EyeZ = 0.93f * StatureCm;
		return Subject;
	}

	// A settings object with the given Shots array text and nothing else. A fresh instance of a config
	// class starts as a copy of its CDO (DefaultGame.ini), so the shots are cleared first.
	UCompanionStageSettings* StageSpecSettings(const FString& ShotsText)
	{
		UCompanionStageSettings* Settings = NewObject<UCompanionStageSettings>(GetTransientPackage());
		Settings->Shots.Reset();
		if (!ShotsText.IsEmpty())
		{
			FArrayProperty* Prop = FindFProperty<FArrayProperty>(UCompanionStageSettings::StaticClass(),
				GET_MEMBER_NAME_CHECKED(UCompanionStageSettings, Shots));
			if (!Prop || !Prop->ImportText_Direct(*ShotsText, Prop->ContainerPtrToValuePtr<void>(Settings), Settings, PPF_None))
			{
				return nullptr;
			}
		}
		return Settings;
	}

	// Two shots exactly as Deployment/stage_layout.py writes them into DefaultGame.ini.
	const TCHAR* StageSpecShotsText =
		TEXT("((Name=\"ARRIVAL\",Frame=Resident,FocalMm=50.0,FilmbackMm=(X=36.0,Y=20.25),Aperture=4.0,Seconds=5.56,")
		TEXT("AccelFrac=0.3,DecelFrac=0.5,MaxRangeCm=473.2,Keys=((RangeCm=473.2,ThetaDeg=-10.0,DzCm=0.0,Screen=(X=0.57,Y=0.53)),")
		TEXT("(RangeCm=412.7,ThetaDeg=-7.5,DzCm=0.0,Screen=(X=0.5852,Y=0.4792)),(RangeCm=352.2,ThetaDeg=-5.0,DzCm=0.0,Screen=(X=0.6003,Y=0.429)),")
		TEXT("(RangeCm=291.7,ThetaDeg=-2.5,DzCm=0.0,Screen=(X=0.6152,Y=0.3793)),(RangeCm=231.2,ThetaDeg=0.0,DzCm=0.0,Screen=(X=0.63,Y=0.33)))),")
		TEXT("(Name=\"HOME\",Frame=Resident,FocalMm=50.0,FilmbackMm=(X=36.0,Y=20.25),Aperture=4.0,Seconds=0.0,")
		TEXT("Keys=((RangeCm=231.2,ThetaDeg=0.0,DzCm=0.0,Screen=(X=0.63,Y=0.33)))),")
		TEXT("(Name=\"FRIEZE\",Frame=World,FocalMm=32.0,FilmbackMm=(X=36.0,Y=20.25),Aperture=4.0,Seconds=13.6,ShiftMm=(X=0.0,Y=-1.5),")
		TEXT("Keys=((Location=(X=748.0,Y=-535.4,Z=140.0),LookAt=(X=973.0,Y=-535.4,Z=140.0)))))");

	float StageSpecAngleDeg(const FVector& A, const FVector& B)
	{
		const FVector NA = A.GetSafeNormal();
		const FVector NB = B.GetSafeNormal();
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(NA, NB), -1.0, 1.0)));
	}
}

// The camera policy (whether the shot moves, whether the pawn may move it), and the data-driven
// stage camera: config parsing, the built-in fallback, and the move's continuity, comfort and beats.
//
// The director itself needs a game world, a tagged resident and a player controller, none of which
// exist under -nullrhi, so everything it decides is factored into pure functions that take a
// settings object, a subject and an env lookup: CompanionStage::BuildPlan, FCompanionStagePath and
// UCompanionStageDirector::BeatAt. Same pattern as EnvConfigSpec: an injected lookup rather than real
// process-environment mutation, so tests cannot leak state into each other or into the editor.
void FCompanionStageSpec::Define()
{
	auto MakeLookup = [](const TMap<FString, FString>& Env)
	{
		return [Env](const TCHAR* Name) -> FString
		{
			const FString* Found = Env.Find(FString(Name));
			return Found ? *Found : FString();
		};
	};

	Describe("the opening shot", [this, MakeLookup]()
	{
		It("moves by default when nothing is configured", [this, MakeLookup]()
		{
			TMap<FString, FString> Env;
			TestTrue("the move by default", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));
		});

		It("accepts explicit positive values", [this, MakeLookup]()
		{
			TMap<FString, FString> Env;
			Env.Add(TEXT("MHLLM_OPENING_SHOT"), TEXT("1"));
			TestTrue("opted in with 1", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));

			Env.Add(TEXT("MHLLM_OPENING_SHOT"), TEXT("true"));
			TestTrue("opted in with true", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));

			Env.Add(TEXT("MHLLM_OPENING_SHOT"), TEXT("TRUE"));
			TestTrue("case insensitive", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));

			Env.Add(TEXT("MHLLM_OPENING_SHOT"), TEXT("yes"));
			TestTrue("opted in with yes", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));
		});

		It("stays static for explicit false values and typos", [this, MakeLookup]()
		{
			TMap<FString, FString> Env;
			Env.Add(TEXT("MHLLM_OPENING_SHOT"), TEXT("0"));
			TestFalse("zero", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));

			Env.Add(TEXT("MHLLM_OPENING_SHOT"), TEXT("false"));
			TestFalse("false", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));

			Env.Add(TEXT("MHLLM_OPENING_SHOT"), TEXT(""));
			TestTrue("empty uses first-load default", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));

			// An unparseable value must fail SAFE. Compose files interpolate blanks and typos
			// happen; neither should start a camera move nobody asked for.
			Env.Add(TEXT("MHLLM_OPENING_SHOT"), TEXT("ture"));
			TestFalse("typo", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));
		});

		It("tolerates the whitespace a compose file leaves behind", [this, MakeLookup]()
		{
			TMap<FString, FString> Env;
			Env.Add(TEXT("MHLLM_OPENING_SHOT"), TEXT(" 1 "));
			TestTrue("padded 1 still opts in", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));
		});
	});

	// The other half of "the camera must not move": even with a static shot, the GameModeBase
	// DefaultPawn's default bindings turn mouse movement into camera yaw/pitch.
	Describe("free-look", [this, MakeLookup]()
	{
		It("is locked when nothing is configured", [this, MakeLookup]()
		{
			TMap<FString, FString> Env;
			TestFalse("locked by default", UCompanionStageDirector::ShouldAllowFreeLook(MakeLookup(Env)));
		});

		It("is handed back only on an explicit opt-in", [this, MakeLookup]()
		{
			TMap<FString, FString> Env;
			Env.Add(TEXT("MHLLM_FREE_LOOK"), TEXT("1"));
			TestTrue("opted in", UCompanionStageDirector::ShouldAllowFreeLook(MakeLookup(Env)));

			Env.Add(TEXT("MHLLM_FREE_LOOK"), TEXT("0"));
			TestFalse("explicitly off", UCompanionStageDirector::ShouldAllowFreeLook(MakeLookup(Env)));
		});

		It("is independent of the opening shot", [this, MakeLookup]()
		{
			// Two separate decisions: a launch stream may want the flythrough AND a locked camera
			// afterwards, and a developer may want to fly around a static scene.
			TMap<FString, FString> Env;
			Env.Add(TEXT("MHLLM_OPENING_SHOT"), TEXT("1"));
			TestTrue("shot on", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));
			TestFalse("look still locked", UCompanionStageDirector::ShouldAllowFreeLook(MakeLookup(Env)));
		});
	});

	// On the real stream GameModeBase's DefaultPawn stood at the map's PlayerStart (the HOME camera):
	// its sphere hung beside her in the lobby still and filled the lens as the move landed, because it
	// is hidden only from views through the pawn itself.
	Describe("the stand-in pawn", [this]()
	{
		It("is hidden from the stage camera, once", [this]()
		{
			UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false);
			ADefaultPawn* Pawn = World ? World->SpawnActor<ADefaultPawn>() : nullptr;
			if (TestNotNull("a DefaultPawn spawns", Pawn))
			{
				TestFalse("visible as GameModeBase spawns it", Pawn->IsHidden());
				TestTrue("hidden by the director", UCompanionStageDirector::HideStandInPawn(Pawn));
				TestTrue("hidden in game", Pawn->IsHidden());
				TestFalse("a second call changes nothing", UCompanionStageDirector::HideStandInPawn(Pawn));
			}
			TestFalse("no pawn, nothing to hide", UCompanionStageDirector::HideStandInPawn(nullptr));
			if (World)
			{
				World->DestroyWorld(/*bInformEngineOfWorld*/ false);
			}
		});
	});

	// The config is what Deployment/stage_layout.py writes from stage_layout.json. If Unreal stops
	// parsing it, the director silently flies the fallback path, so the format is pinned here.
	Describe("stage settings", [this]()
	{
		It("parse shots in the format stage_layout.py writes", [this]()
		{
			UCompanionStageSettings* Settings = StageSpecSettings(StageSpecShotsText);
			if (!TestNotNull("the Shots text imports", Settings))
			{
				return;
			}
			TestEqual("three shots", Settings->Shots.Num(), 3);
			const FCompanionStageShot* Arrival = Settings->FindShot(TEXT("ARRIVAL"));
			if (!TestNotNull("ARRIVAL found by name", Arrival))
			{
				return;
			}
			TestTrue("resident frame", Arrival->Frame == ECompanionStageFrame::Resident);
			TestEqual("focal", Arrival->FocalMm, 50.0f);
			TestEqual("filmback height", Arrival->FilmbackMm.Y, 20.25);
			TestEqual("five keys", Arrival->Keys.Num(), 5);
			TestEqual("start range", Arrival->Keys[0].RangeCm, 473.2f);
			TestEqual("start bearing", Arrival->Keys[0].ThetaDeg, -10.0f);
			TestEqual("max range", Arrival->MaxRangeCm, 473.2f);
			TestEqual("home composition x", Arrival->Keys.Last().Screen.X, 0.63, 1e-6);
			TestEqual("home composition y", Arrival->Keys.Last().Screen.Y, 0.33, 1e-6);
			const FCompanionStageShot* Frieze = Settings->FindShot(TEXT("FRIEZE"));
			if (TestNotNull("FRIEZE found", Frieze))
			{
				TestTrue("world frame", Frieze->Frame == ECompanionStageFrame::World);
				TestEqual("world key x", Frieze->Keys[0].Location.X, 748.0);
				TestEqual("world look-at x", Frieze->Keys[0].LookAt.X, 973.0);
				TestEqual("shift y", Frieze->ShiftMm.Y, -1.5);
			}
			TestTrue("an opening and a home make a data-driven opening", Settings->HasOpening());
		});

		It("load the section generated from stage_layout.json", [this]()
		{
			const UCompanionStageSettings* Settings = GetDefault<UCompanionStageSettings>();
			TestTrue("written by stage_layout.py", Settings->LayoutSource.Contains(TEXT("stage_layout.json")));
			TestTrue("the opening is data-driven", Settings->HasOpening());
			const FCompanionStageShot* Arrival = Settings->FindShot(Settings->OpeningShot);
			const FCompanionStageShot* Home = Settings->FindShot(Settings->HomeShot);
			if (!TestNotNull("opening shot", Arrival) || !TestNotNull("home shot", Home))
			{
				return;
			}
			TestEqual("ARRIVAL keys", Arrival->Keys.Num(), 5);
			TestEqual("HOME range", Home->Keys[0].RangeCm, 231.2f);
			TestEqual("HOME eyes x", Home->Keys[0].Screen.X, 0.63, 1e-6);
			TestEqual("HOME eyes y", Home->Keys[0].Screen.Y, 0.33, 1e-6);
			TestNotNull("the lobby still", Settings->FindShot(Settings->LobbyShot));
			TestNotNull("a portrait home", Settings->FindShotFor(Settings->HomeShot, true));
			TestEqual("the distance salutation", Settings->GreetingLine, FString(TEXT("Oh, hello.")));
			TestTrue("never turns her more than 30 degrees", FMath::Abs(Settings->WorkTurnDegrees) <= 30.0f);
		});

		// On the real stream a visitor's MatchViewportRes turned the game to 1080x1920 long after
		// BeginPlay had solved a landscape plan, and HOME's 16:9 lens filled the portrait frame with her
		// chest. The director now re-solves the plan when the viewport turns; these are its two inputs.
		It("take the portrait shots when the viewport is taller than wide", [this]()
		{
			TestFalse("1920x1080 is landscape", UCompanionStageDirector::IsPortraitViewport(FVector2D(1920.0, 1080.0), false));
			TestTrue("1080x1920 is portrait", UCompanionStageDirector::IsPortraitViewport(FVector2D(1080.0, 1920.0), false));
			TestFalse("a square is landscape", UCompanionStageDirector::IsPortraitViewport(FVector2D(1080.0, 1080.0), true));
			TestTrue("no area yet keeps portrait", UCompanionStageDirector::IsPortraitViewport(FVector2D(0.0, 0.0), true));
			TestFalse("no area yet keeps landscape", UCompanionStageDirector::IsPortraitViewport(FVector2D(1080.0, 0.0), false));

			const FCompanionStagePlan Landscape = CompanionStage::BuildPlan(GetDefault<UCompanionStageSettings>(),
				StageSpecSubject(170.0f), StageSpecLookup(TMap<FString, FString>()), false);
			const FCompanionStagePlan Portrait = CompanionStage::BuildPlan(GetDefault<UCompanionStageSettings>(),
				StageSpecSubject(170.0f), StageSpecLookup(TMap<FString, FString>()), true);
			TestTrue("landscape HOME on a 16:9 filmback", Landscape.Home.FilmbackMm.X > Landscape.Home.FilmbackMm.Y);
			TestTrue("portrait HOME on a 9:16 filmback", Portrait.Home.FilmbackMm.X < Portrait.Home.FilmbackMm.Y);
			TestTrue("portrait lobby on a 9:16 filmback", Portrait.Lobby.FilmbackMm.X < Portrait.Lobby.FilmbackMm.Y);
		});

		It("fall back to the built-in path when the config has no shots", [this]()
		{
			UCompanionStageSettings* Empty = StageSpecSettings(FString());
			if (!TestNotNull("empty settings", Empty))
			{
				return;
			}
			TestFalse("no opening without shots", Empty->HasOpening());
			const FCompanionStageSubject Subject = StageSpecSubject(170.0f);
			TMap<FString, FString> Env;
			const FCompanionStagePlan Plan = CompanionStage::BuildPlan(Empty, Subject, StageSpecLookup(Env));
			TestFalse("not from config", Plan.bFromConfig);
			TestEqual("default flythrough duration", Plan.MoveSeconds, 11.0f);
			const TArray<FStageCameraKey> Keys = UCompanionStageDirector::FallbackKeys(Subject.Mark, Subject.EyeZ, 11.0f);
			const TArray<FVector>& Points = Plan.Opening.GetPoints();
			if (TestEqual("six keys", Points.Num(), Keys.Num()))
			{
				for (int32 i = 0; i < Keys.Num(); ++i)
				{
					TestTrue(FString::Printf(TEXT("key %d unchanged"), i), Points[i].Equals(Keys[i].Position, 0.01));
				}
			}
			TestFalse("no cut: the lobby is the first key", Plan.bCutIntoMove);
			TestTrue("lands on the last key", Plan.Opening.PositionAt(Plan.MoveSeconds).Equals(Keys.Last().Position, 0.1));
			TestTrue("an empty config is noted in the log", Plan.Notes.Num() > 0);
		});

		It("keep the built-in keys where the director always had them", [this]()
		{
			// The rc1 map's mark and the 0.93 x bounds eyeline (design 6.4).
			const TArray<FStageCameraKey> Keys = UCompanionStageDirector::FallbackKeys(FVector(330.0, 20.0, 0.0), 179.49f, 11.0f);
			TestTrue("first key", Keys[0].Position.Equals(FVector(-1220.0, -200.0, 280.0), 0.01));
			TestTrue("first aim", Keys[0].LookAt.Equals(FVector(780.0, -60.0, 185.0), 0.01));
			TestTrue("last key", Keys.Last().Position.Equals(FVector(-100.0, 20.0, 171.49), 0.01));
			TestTrue("last aim", Keys.Last().LookAt.Equals(FVector(330.0, 12.0, 161.49), 0.01));
			TestEqual("timed across the flight", Keys.Last().Time, 11.0f);
		});
	});

	Describe("the opening move", [this]()
	{
		for (const float Stature : { 150.0f, 170.0f, 193.0f })
		{
			const FString Label = FString::Printf(TEXT("at %.0f cm"), Stature);

			It(FString::Printf(TEXT("has no jump in position or aim between 60 fps frames %s"), *Label), [this, Stature]()
			{
				const FCompanionStagePlan Plan = CompanionStage::BuildPlan(GetDefault<UCompanionStageSettings>(),
					StageSpecSubject(Stature), StageSpecLookup(TMap<FString, FString>()));
				if (!TestTrue("data-driven", Plan.bFromConfig))
				{
					return;
				}
				const float Dt = 1.0f / 60.0f;
				const int32 Frames = FMath::CeilToInt((Plan.MoveSeconds + 0.5f) / Dt);
				float MaxStep = 0.0f;
				float MaxStepChange = 0.0f;
				float MaxAimTurn = 0.0f;
				float MaxLookJump = 0.0f;
				FVector PrevPos = Plan.Opening.PositionAt(0.0f);
				FVector PrevAim = Plan.Opening.LookAtAt(0.0f) - PrevPos;
				FVector PrevLook = Plan.Opening.LookAtAt(0.0f);
				float PrevStep = 0.0f;
				for (int32 i = 1; i <= Frames; ++i)
				{
					const float T = FMath::Min(i * Dt, Plan.MoveSeconds);
					const FVector Pos = Plan.Opening.PositionAt(T);
					const FVector Look = Plan.Opening.LookAtAt(T);
					const float Step = FVector::Dist(Pos, PrevPos);
					MaxStep = FMath::Max(MaxStep, Step);
					MaxStepChange = FMath::Max(MaxStepChange, FMath::Abs(Step - PrevStep));
					MaxAimTurn = FMath::Max(MaxAimTurn, StageSpecAngleDeg(Look - Pos, PrevAim));
					MaxLookJump = FMath::Max(MaxLookJump, (float)FVector::Dist(Look, PrevLook));
					PrevPos = Pos;
					PrevAim = Look - Pos;
					PrevLook = Look;
					PrevStep = Step;
				}
				// 1.0 m/s is the hard speed budget: under 1.67 cm a frame. Acceleration under 1 m/s^2
				// changes the step by under 0.03 cm a frame; 0.1 cm still catches any seam at a key.
				TestTrue(FString::Printf(TEXT("largest step %.3f cm/frame"), MaxStep), MaxStep <= 100.0f / 60.0f);
				TestTrue(FString::Printf(TEXT("largest change of step %.4f cm"), MaxStepChange), MaxStepChange <= 0.1f);
				// The aim turns under HFOV/7 a second (5.66 deg/s at 50 mm): under 0.1 deg a frame.
				TestTrue(FString::Printf(TEXT("largest aim turn %.4f deg/frame"), MaxAimTurn), MaxAimTurn <= 0.1f);
				TestTrue(FString::Printf(TEXT("aim point jump %.4f cm"), MaxLookJump), MaxLookJump <= 0.01f);
			});

			It(FString::Printf(TEXT("keeps the comfort budget %s"), *Label), [this, Stature]()
			{
				const FCompanionStageSubject Subject = StageSpecSubject(Stature);
				const FCompanionStagePlan Plan = CompanionStage::BuildPlan(GetDefault<UCompanionStageSettings>(),
					Subject, StageSpecLookup(TMap<FString, FString>()));
				TestTrue(FString::Printf(TEXT("duration %.2f s within 5-8 s"), Plan.MoveSeconds), Plan.MoveSeconds >= 5.0f - KINDA_SMALL_NUMBER && Plan.MoveSeconds <= 8.0f);
				TestTrue(FString::Printf(TEXT("peak speed %.3f m/s <= 1.0"), Plan.PeakSpeedCmPerSecond / 100.0f), Plan.PeakSpeedCmPerSecond <= 100.0f);
				TestTrue(FString::Printf(TEXT("time to contact %.2f s >= 4"), Plan.MinTimeToContactSeconds), Plan.MinTimeToContactSeconds >= 4.0f);
				TestTrue(FString::Printf(TEXT("nearest %.2f m >= 2"), Plan.MinEyeDistanceCm / 100.0f), Plan.MinEyeDistanceCm >= 200.0f);
				// Acceleration at 240 Hz, never over 1 m/s^2.
				const float Dt = 1.0f / 240.0f;
				float MaxAccel = 0.0f;
				float MaxLensOffEye = 0.0f;
				for (float T = 2.0f * Dt; T <= Plan.MoveSeconds; T += Dt)
				{
					const FVector P0 = Plan.Opening.PositionAt(T - 2.0f * Dt);
					const FVector P1 = Plan.Opening.PositionAt(T - Dt);
					const FVector P2 = Plan.Opening.PositionAt(T);
					MaxAccel = FMath::Max(MaxAccel, (float)((P2 - 2.0 * P1 + P0).Size() / (Dt * Dt)));
					MaxLensOffEye = FMath::Max(MaxLensOffEye, (float)FMath::Abs(P2.Z - Plan.Aim.Z));
				}
				TestTrue(FString::Printf(TEXT("peak acceleration %.3f m/s^2 <= 1.0"), MaxAccel / 100.0f), MaxAccel <= 100.0f);
				TestTrue(FString::Printf(TEXT("lens %.2f cm from her eye height"), MaxLensOffEye), MaxLensOffEye <= 10.0f);
				TestTrue("never turns her more than 30 degrees", FMath::Abs(Plan.WorkTurnDegrees) <= 30.0f);
			});

			It(FString::Printf(TEXT("lands exactly on the home framing %s"), *Label), [this, Stature]()
			{
				const FCompanionStagePlan Plan = CompanionStage::BuildPlan(GetDefault<UCompanionStageSettings>(),
					StageSpecSubject(Stature), StageSpecLookup(TMap<FString, FString>()));
				const FVector End = Plan.Opening.PositionAt(Plan.MoveSeconds);
				TestTrue(FString::Printf(TEXT("HOME within 1 mm of the last key (%.3f cm)"), FVector::Dist(End, Plan.Home.Location)),
					FVector::Dist(End, Plan.Home.Location) <= 0.1);
				const FVector2D Eyes = Plan.Opening.ScreenAt(Plan.MoveSeconds);
				TestEqual("eyes land at u 0.63", Eyes.X, 0.63, 0.02);
				TestEqual("eyes land at v 0.33", Eyes.Y, 0.33, 0.02);
				TestEqual("home distance 1.36 x stature", (float)FVector::Dist2D(Plan.Home.Location, Plan.Aim), 1.36f * Stature, 1.0f);
				TestTrue("the lobby still is a different place: starting the move is a flagged cut", Plan.bCutIntoMove);
			});

			It(FString::Printf(TEXT("never goes backwards %s"), *Label), [this, Stature]()
			{
				const FCompanionStagePlan Plan = CompanionStage::BuildPlan(GetDefault<UCompanionStageSettings>(),
					StageSpecSubject(Stature), StageSpecLookup(TMap<FString, FString>()));
				TestEqual("starts at 0", Plan.Opening.ProgressAt(0.0f), 0.0f);
				TestEqual("ends at 1", Plan.Opening.ProgressAt(Plan.MoveSeconds), 1.0f, 1e-5f);
				TestEqual("holds 1 after the end", Plan.Opening.ProgressAt(Plan.MoveSeconds + 2.0f), 1.0f, 1e-5f);
				float Prev = 0.0f;
				bool bMonotonic = true;
				for (float T = 0.0f; T <= Plan.MoveSeconds + 0.25f; T += 1.0f / 240.0f)
				{
					const float P = Plan.Opening.ProgressAt(T);
					bMonotonic &= P >= Prev - 1e-6f;
					Prev = P;
				}
				TestTrue("progress is monotonic at 240 Hz", bMonotonic);
			});
		}

		It("is close to the design's timing for the 170 cm reference", [this]()
		{
			const FCompanionStagePlan Plan = CompanionStage::BuildPlan(GetDefault<UCompanionStageSettings>(),
				StageSpecSubject(170.0f), StageSpecLookup(TMap<FString, FString>()));
			// design.md 6.2 / 3.1: 5.56 s, the look-up at 2.55 s, the turn-yield line at 5.75 s.
			TestEqual("move duration", Plan.MoveSeconds, 5.56f, 0.15f);
			TestEqual("look-up", Plan.LookUpAt, 2.55f, 0.25f);
			TestEqual("turn-yield line", Plan.SpeakAt, 5.75f, 0.25f);
			TestEqual("path length", Plan.Opening.GetLength(), 250.0f, 10.0f);
			const FVector2D Shift = CompanionStage::SensorShiftFor(Plan.Home.Location, Plan.Aim, Plan.Home.Screen, 50.0f, FVector2D(36.0, 20.25));
			TestEqual("HOME shift x (design -4.68 mm)", Shift.X, -4.68, 0.01);
			TestEqual("HOME shift y (design -3.44 mm)", Shift.Y, -3.44, 0.01);
		});
	});

	Describe("beats", [this]()
	{
		auto CheckOrder = [this](const FCompanionStagePlan& Plan, const TArray<EStageBeat>& Expected)
		{
			TArray<EStageBeat> Seen;
			EStageBeat Prev = EStageBeat::Flythrough;
			bool bOrdered = true;
			const float End = Plan.HoldSeconds + FMath::Max(Plan.SpeakAt, Plan.MoveSeconds) + 1.0f;
			for (float T = 0.0f; T <= End; T += 1.0f / 60.0f)
			{
				const EStageBeat Beat = UCompanionStageDirector::BeatAt(Plan, T);
				bOrdered &= (uint8)Beat >= (uint8)Prev;
				if (Seen.Num() == 0 || Seen.Last() != Beat)
				{
					Seen.Add(Beat);
				}
				Prev = Beat;
			}
			TestTrue("beats only advance", bOrdered);
			TestTrue("the expected sequence", Seen == Expected);
		};

		It("advance flythrough, settle, live in the data-driven opening", [this, CheckOrder]()
		{
			const FCompanionStagePlan Plan = CompanionStage::BuildPlan(GetDefault<UCompanionStageSettings>(),
				StageSpecSubject(170.0f), StageSpecLookup(TMap<FString, FString>()));
			CheckOrder(Plan, { EStageBeat::Flythrough, EStageBeat::Settle, EStageBeat::Live });
			TestTrue("the lobby is held first", UCompanionStageDirector::BeatAt(Plan, Plan.HoldSeconds - 0.1f) == EStageBeat::Flythrough);
		});

		It("advance flythrough, turn, speak, live on the built-in path", [this, CheckOrder]()
		{
			const FCompanionStagePlan Plan = CompanionStage::BuildFallbackPlan(StageSpecSubject(170.0f), StageSpecLookup(TMap<FString, FString>()));
			CheckOrder(Plan, { EStageBeat::Flythrough, EStageBeat::Turn, EStageBeat::Speak, EStageBeat::Live });
		});

		for (const float Stature : { 150.0f, 170.0f, 193.0f })
		{
			It(FString::Printf(TEXT("fall in order at %.0f cm: greeting, look-up, turn, landing, line"), Stature), [this, Stature]()
			{
				const FCompanionStagePlan Plan = CompanionStage::BuildPlan(GetDefault<UCompanionStageSettings>(),
					StageSpecSubject(Stature), StageSpecLookup(TMap<FString, FString>()));
				TestTrue("greeting while the camera walks", Plan.GreetingAt >= 0.0f && Plan.GreetingAt < Plan.LookUpAt);
				TestTrue("turn after the look-up", Plan.LookUpAt <= Plan.TurnStart && Plan.TurnStart < Plan.TurnEnd);
				TestTrue("turned before the camera lands", Plan.TurnEnd <= Plan.MoveSeconds);
				TestTrue("settles before the line", Plan.SettleAt <= Plan.SpeakAt);
				TestTrue("the line never comes before the landing", Plan.SpeakAt >= Plan.MoveSeconds);
				TestTrue("the line within 7 s of the cut", Plan.SpeakAt <= 7.0f);
				// The layout picks the side she works toward (WorkTurnDegrees: + to her right, - to her left; the 1979
				// room's printout lies on her left), so the turn is checked by its size and its direction of travel.
				float Prev = CompanionStage::ResidentYawOffsetAt(Plan, 0.0f);
				TestEqual("at work: turned 30 degrees away", FMath::Abs(Prev), 30.0f, 0.01f);
				const float Side = Prev < 0.0f ? -1.0f : 1.0f;
				bool bOneWay = true;
				for (float T = 0.0f; T <= Plan.MoveSeconds; T += 1.0f / 60.0f)
				{
					const float Offset = CompanionStage::ResidentYawOffsetAt(Plan, T);
					bOneWay &= Side * Offset <= Side * Prev + 1e-4f && Side * Offset >= -1e-4f && FMath::Abs(Offset) <= 30.0f;
					Prev = Offset;
				}
				TestTrue("turns one way, never more than 30 degrees", bOneWay);
				TestEqual("home by the landing", CompanionStage::ResidentYawOffsetAt(Plan, Plan.MoveSeconds), 0.0f, 0.01f);
			});
		}
	});

	Describe("environment", [this]()
	{
		It("MHLLM_FLY_SECONDS caps the move but never below its comfort floor", [this]()
		{
			const UCompanionStageSettings* Settings = GetDefault<UCompanionStageSettings>();
			const FCompanionStageSubject Subject = StageSpecSubject(170.0f);
			const FCompanionStagePlan Base = CompanionStage::BuildPlan(Settings, Subject, StageSpecLookup(TMap<FString, FString>()));

			TMap<FString, FString> TooFast;
			TooFast.Add(TEXT("MHLLM_FLY_SECONDS"), TEXT("2"));
			const FCompanionStagePlan Fast = CompanionStage::BuildPlan(Settings, Subject, StageSpecLookup(TooFast));
			TestEqual("a 2 s cap is below the floor and ignored", Fast.MoveSeconds, Base.MoveSeconds);
			TestTrue("and the log says so", Fast.Notes.ContainsByPredicate([](const FString& N) { return N.Contains(TEXT("MHLLM_FLY_SECONDS")); }));

			TMap<FString, FString> Slow;
			Slow.Add(TEXT("MHLLM_FLY_SECONDS"), TEXT("11.0"));
			const FCompanionStagePlan Long = CompanionStage::BuildPlan(Settings, Subject, StageSpecLookup(Slow));
			TestEqual("the compose default (11) is above the derived move: no effect", Long.MoveSeconds, Base.MoveSeconds);

			TMap<FString, FString> Builtin;
			Builtin.Add(TEXT("MHLLM_FLY_SECONDS"), TEXT("7"));
			const FCompanionStagePlan Fallback = CompanionStage::BuildFallbackPlan(Subject, StageSpecLookup(Builtin));
			TestEqual("on the built-in path it still sets the flythrough", Fallback.MoveSeconds, 7.0f);
		});

		It("MHLLM_OPENING_DELAY sets the lobby hold", [this]()
		{
			TMap<FString, FString> Env;
			Env.Add(TEXT("MHLLM_OPENING_DELAY"), TEXT("1.5"));
			const FCompanionStagePlan Plan = CompanionStage::BuildPlan(GetDefault<UCompanionStageSettings>(),
				StageSpecSubject(170.0f), StageSpecLookup(Env));
			TestEqual("hold", Plan.HoldSeconds, 1.5f);
			TestTrue("still moving after the hold", UCompanionStageDirector::BeatAt(Plan, 1.6f) == EStageBeat::Flythrough);
		});
	});

	// Design 3.1 and 10.6: a session, not the level load, starts the opening once someone is there.
	Describe("sessions", [this, MakeLookup]()
	{
		It("start when a viewer connects to a streamed process, and at load otherwise", [this, MakeLookup]()
		{
			TMap<FString, FString> Env;
			TestTrue("a streaming command line waits for its viewer",
				UCompanionStageDirector::ResolveTrigger(MakeLookup(Env), TEXT("-RenderOffScreen -PixelStreamingUrl=ws://127.0.0.1:8888 -AudioMixer")) == EStageTrigger::Viewer);
			TestTrue("the 5.5 name too",
				UCompanionStageDirector::ResolveTrigger(MakeLookup(Env), TEXT("-PixelStreamingConnectionURL=ws://host:80")) == EStageTrigger::Viewer);
			TestTrue("a native window or a kiosk starts at load",
				UCompanionStageDirector::ResolveTrigger(MakeLookup(Env), TEXT("-windowed -ResX=1280")) == EStageTrigger::Load);
			TestTrue("an empty URL is not a stream",
				UCompanionStageDirector::ResolveTrigger(MakeLookup(Env), TEXT("-PixelStreamingURL= -windowed")) == EStageTrigger::Load);
			Env.Add(TEXT("MHLLM_OPENING_TRIGGER"), TEXT("load"));
			TestTrue("MHLLM_OPENING_TRIGGER=load wins over the command line",
				UCompanionStageDirector::ResolveTrigger(MakeLookup(Env), TEXT("-PixelStreamingUrl=ws://127.0.0.1:8888")) == EStageTrigger::Load);
			Env.Add(TEXT("MHLLM_OPENING_TRIGGER"), TEXT(" Viewer "));
			TestTrue("MHLLM_OPENING_TRIGGER=viewer without a stream",
				UCompanionStageDirector::ResolveTrigger(MakeLookup(Env), TEXT("")) == EStageTrigger::Viewer);
		});

		It("keep the operator console from stream visitors", [this, MakeLookup]()
		{
			TMap<FString, FString> Env;
			TestFalse("a streamed process ignores /console",
				UCompanionRuntimeSubsystem::ConsoleCommandAllowed(MakeLookup(Env), TEXT("-PixelStreamingUrl=ws://127.0.0.1:8888")));
			TestTrue("a native window or a kiosk keeps it",
				UCompanionRuntimeSubsystem::ConsoleCommandAllowed(MakeLookup(Env), TEXT("-windowed")));
			Env.Add(TEXT("MHLLM_CONSOLE_COMMAND"), TEXT("1"));
			TestTrue("MHLLM_CONSOLE_COMMAND=1 gives it to a stream operator",
				UCompanionRuntimeSubsystem::ConsoleCommandAllowed(MakeLookup(Env), TEXT("-PixelStreamingUrl=ws://127.0.0.1:8888")));
			Env.Add(TEXT("MHLLM_CONSOLE_COMMAND"), TEXT("0"));
			TestFalse("MHLLM_CONSOLE_COMMAND=0 takes it from a kiosk",
				UCompanionRuntimeSubsystem::ConsoleCommandAllowed(MakeLookup(Env), TEXT("-windowed")));
		});

		It("replay the opening only as a kiosk reset, never with a viewer connected", [this]()
		{
			TestFalse("off by default", UCompanionStageDirector::ShouldReplay(0.0f, 1000.0f, false, true));
			TestFalse("never while a viewer is connected", UCompanionStageDirector::ShouldReplay(60.0f, 1000.0f, true, true));
			TestFalse("not before the visitor has been idle that long", UCompanionStageDirector::ShouldReplay(60.0f, 59.0f, false, true));
			TestFalse("never during the opening", UCompanionStageDirector::ShouldReplay(60.0f, 1000.0f, false, false));
			TestTrue("idle with nobody watching", UCompanionStageDirector::ShouldReplay(60.0f, 60.0f, false, true));
		});
	});

	// Design 3.2, row by row.
	Describe("input", [this]()
	{
		It("is G in the lobby, and a turn typed there is held for the landing", [this]()
		{
			for (const EStageInput In : { EStageInput::Key, EStageInput::Click, EStageInput::Escape, EStageInput::Enter })
			{
				const FStageInputResponse R = UCompanionStageDirector::RespondToInput(EStagePhase::Lobby, In, 4.0f);
				TestTrue("starts the session", R.bStartSession);
				TestEqual("the cut inside 0.3 s later", R.HoldSeconds, 0.3f);
				TestFalse("no cut to HOME from the lobby", R.bCutToHome);
				TestFalse("nothing held", R.bHoldTurn);
			}
			const FStageInputResponse Turn = UCompanionStageDirector::RespondToInput(EStagePhase::Lobby, EStageInput::Turn, 4.0f);
			TestTrue("a turn starts the session", Turn.bStartSession);
			TestTrue("and is held to be answered on landing", Turn.bHoldTurn);
			TestTrue("she does not yield a floor the visitor already took", Turn.bDropLine);
		});

		It("cuts to HOME during the move, keeping or dropping the lines by input", [this]()
		{
			const FStageInputResponse Soon = UCompanionStageDirector::RespondToInput(EStagePhase::Arrival, EStageInput::Key, 0.6f);
			TestTrue("a key cuts to HOME", Soon.bCutToHome);
			TestTrue("the turn-yield line is dropped", Soon.bDropLine);
			TestTrue("'Oh, hello.' due within 1 s is said", Soon.bGreetNow);
			const FStageInputResponse Late = UCompanionStageDirector::RespondToInput(EStagePhase::Arrival, EStageInput::Click, 2.5f);
			TestTrue("a click cuts to HOME", Late.bCutToHome);
			TestTrue("a greeting 2.5 s off is dropped", Late.bDropGreeting && !Late.bGreetNow);
			const FStageInputResponse Said = UCompanionStageDirector::RespondToInput(EStagePhase::Arrival, EStageInput::Key, -1.0f);
			TestFalse("an already-said greeting is not repeated", Said.bGreetNow || Said.bDropGreeting);
			const FStageInputResponse Esc = UCompanionStageDirector::RespondToInput(EStagePhase::Arrival, EStageInput::Escape, 2.5f);
			TestTrue("Esc cuts to HOME", Esc.bCutToHome);
			TestTrue("and drops nothing", Esc.bKeepLines && !Esc.bDropLine && !Esc.bDropGreeting);
			TestTrue("the greeting comes now", Esc.bGreetNow);
			const FStageInputResponse Turn = UCompanionStageDirector::RespondToInput(EStagePhase::Arrival, EStageInput::Turn, 0.5f);
			TestTrue("a turn cuts to HOME", Turn.bCutToHome);
			TestTrue("pending lines are dropped", Turn.bDropLine && Turn.bDropGreeting && !Turn.bGreetNow);
			TestFalse("and she answers it now", Turn.bHoldTurn);
		});

		It("leaves the turn-yield line and the live conversation alone", [this]()
		{
			for (const EStagePhase Phase : { EStagePhase::Line, EStagePhase::Live })
			{
				for (const EStageInput In : { EStageInput::Key, EStageInput::Click, EStageInput::Escape, EStageInput::Turn })
				{
					const FStageInputResponse R = UCompanionStageDirector::RespondToInput(Phase, In, -1.0f);
					TestFalse("no session start", R.bStartSession);
					TestFalse("no cut", R.bCutToHome);
					TestFalse("no line dropped", R.bDropLine);
				}
			}
		});
	});

	Describe("the live framing", [this]()
	{
		It("punches in to CLOSE on long replies at a sentence boundary", [this]()
		{
			const FString Text = TEXT("Ten letters. Then the rest of a reply that goes on for a good while longer than that.");
			TestTrue("a short reply stays on HOME", UCompanionStageDirector::CloseCutTime(Text, 4.0f) < 0.0f);
			const float At = UCompanionStageDirector::CloseCutTime(Text, 14.0f);
			TestEqual("the first boundary 1.5 s or more in", At, 14.0f * 12.0f / Text.Len(), 0.01f);
			TestTrue("never inside the first 1.5 s", UCompanionStageDirector::CloseCutTime(TEXT("Hi. Then a long tail with no stop"), 8.0f) < 0.0f);
			TestTrue("no boundary, no punch-in", UCompanionStageDirector::CloseCutTime(TEXT("one long clause without a full stop"), 9.0f) < 0.0f);
		});

		It("has a CLOSE framing on HOME's spot with the longer lens", [this]()
		{
			const FCompanionStagePlan Plan = CompanionStage::BuildPlan(GetDefault<UCompanionStageSettings>(),
				StageSpecSubject(170.0f), StageSpecLookup(TMap<FString, FString>()));
			if (!TestTrue("CLOSE solved from the layout", Plan.Close.bValid))
			{
				return;
			}
			TestTrue("from HOME's position", Plan.Close.Location.Equals(Plan.Home.Location, 0.1));
			TestEqual("80 mm", Plan.Close.FocalMm, 80.0f, 0.01f);
			TestTrue("her eyes held where HOME holds them", Plan.Close.Screen.Equals(Plan.Home.Screen, 0.02));
		});

		It("breathes by rotation only, small and slow", [this]()
		{
			const float A = 0.15f;
			float MaxYaw = 0.0f, MaxPitch = 0.0f, MaxStep = 0.0f, MaxRoll = 0.0f;
			FRotator Prev = CompanionStage::BreathOffset(0.0f, A);
			for (int32 Frame = 1; Frame < 600 * 60; ++Frame)
			{
				const FRotator R = CompanionStage::BreathOffset(Frame / 60.0f, A);
				MaxYaw = FMath::Max(MaxYaw, FMath::Abs((float)R.Yaw));
				MaxPitch = FMath::Max(MaxPitch, FMath::Abs((float)R.Pitch));
				MaxRoll = FMath::Max(MaxRoll, FMath::Abs((float)R.Roll));
				MaxStep = FMath::Max(MaxStep, (float)FMath::Max(FMath::Abs(R.Yaw - Prev.Yaw), FMath::Abs(R.Pitch - Prev.Pitch)));
				Prev = R;
			}
			TestEqual("never rolls", MaxRoll, 0.0f);
			TestTrue(FString::Printf(TEXT("yaw within %.2f deg (%.3f)"), A, MaxYaw), MaxYaw <= A + 1e-4f && MaxYaw > 0.5f * A);
			TestTrue(FString::Printf(TEXT("pitch within %.2f deg (%.3f)"), 0.6f * A, MaxPitch), MaxPitch <= 0.6f * A + 1e-4f);
			TestTrue(FString::Printf(TEXT("at most %.4f deg a 60 fps frame"), MaxStep), MaxStep <= 0.6f * A / 60.0f);
			TestTrue("nothing at amplitude 0", CompanionStage::BreathOffset(12.3f, 0.0f).IsZero());
		});
	});

	// UTapeReelSpinner used to add to a read-back FRotator's Pitch, which Unreal normalises to
	// [-90, 90]: every reel stopped at a quarter turn. The spin is now a quaternion about the axle.
	Describe("tape reels", [this]()
	{
		for (const FRotator Rest : { FRotator(0.0, 90.0, 0.0), FRotator(0.0, 90.0, -90.0), FRotator(0.0, -35.0, -90.0) })
		{
			It(FString::Printf(TEXT("turn past a quarter turn about their own axle (rest %s)"), *Rest.ToCompactString()), [this, Rest]()
			{
				const FQuat Base = Rest.Quaternion();
				const FVector Axle = UTapeReelSpinner::AxleOf(Base);
				TestTrue("the axle is the yawed Y axis", Axle.Equals(FRotator(0.0, Rest.Yaw, 0.0).RotateVector(FVector::YAxisVector), 1e-4));
				// What the engine stores and gives back between ticks: the rotator of the quaternion,
				// rebuilt into a quaternion. The spinner never reads it back, but whatever it sets has to
				// survive it.
				const FVector Spoke0 = Base.RotateVector(FVector::XAxisVector);
				double Angle = 0.0;
				double Swept = 0.0;
				double AxleDrift = 0.0;
				FVector PrevSpoke = Spoke0;
				for (int32 Frame = 1; Frame <= 90; ++Frame)
				{
					Angle = FMath::Fmod(Angle + 150.0 / 60.0, 360.0);
					const FQuat Stored = UTapeReelSpinner::SpunRotation(Base, Angle).Rotator().Quaternion();
					const FVector Spoke = Stored.RotateVector(FVector::XAxisVector);
					AxleDrift = FMath::Max(AxleDrift, (Stored.RotateVector(Base.Inverse().RotateVector(Axle)) - Axle).Size());
					Swept += FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(FVector::CrossProduct(PrevSpoke, Spoke), Axle),
						FVector::DotProduct(PrevSpoke, Spoke)));
					PrevSpoke = Spoke;
				}
				TestEqual("1.5 s at 150 deg/s sweeps 225 deg", (float)Swept, 225.0f, 0.5f);
				TestTrue(FString::Printf(TEXT("the axle does not move (%.5f)"), AxleDrift), AxleDrift < 1e-3);
				TestTrue("the hub stays a hub: the axle is unit length", FMath::IsNearlyEqual(Axle.Size(), 1.0, 1e-4));
			});
		}
	});

	// Design 10.7: the set shows her state, so a faceless resident still reads as present.
	Describe("the set as a state display", [this]()
	{
		It("lights the INQUIRY lamp only while the visitor types", [this]()
		{
			TestEqual("listening: lit (1.5 -> 150 cd/m2)", CompanionSet::InquiryLampScale(ECompanionPresence::Listening), 100.0f);
			for (const ECompanionPresence P : { ECompanionPresence::Idle, ECompanionPresence::Thinking, ECompanionPresence::Speaking })
			{
				TestEqual("otherwise off", CompanionSet::InquiryLampScale(P), 1.0f);
			}
		});

		It("runs the reacting tape unit by her state: search while thinking, read while speaking", [this]()
		{
			const ECompanionPresence T = ECompanionPresence::Thinking;
			TestTrue("forward first", CompanionSet::TapeDegreesPerSecond(T, 0.4f, 150.0f) > 0.0f);
			TestEqual("then a stop", CompanionSet::TapeDegreesPerSecond(T, 0.95f, 150.0f), 0.0f);
			TestTrue("then back", CompanionSet::TapeDegreesPerSecond(T, 1.3f, 150.0f) < 0.0f);
			TestTrue("every 1.6 s", CompanionSet::TapeDegreesPerSecond(T, 2.0f, 150.0f) > 0.0f);
			float Net = 0.0f;
			for (int32 i = 0; i < 160; ++i)
			{
				Net += CompanionSet::TapeDegreesPerSecond(T, i / 100.0f, 150.0f) / 100.0f;
			}
			TestTrue(FString::Printf(TEXT("searching gains ground each cycle (%.0f deg)"), Net), Net > 0.0f);
			TestEqual("reads at 0.5 rev/s while she speaks", CompanionSet::TapeDegreesPerSecond(ECompanionPresence::Speaking, 3.0f, 150.0f), 180.0f);
			TestEqual("waits while the visitor types", CompanionSet::TapeDegreesPerSecond(ECompanionPresence::Listening, 3.0f, 150.0f), 0.0f);
			TestEqual("creeps when idle", CompanionSet::TapeDegreesPerSecond(ECompanionPresence::Idle, 3.0f, 160.0f), 40.0f);
		});

		It("keeps the wall clock on host time", [this]()
		{
			float H = 0.0f, M = 0.0f;
			CompanionSet::ClockAngles(15, 0, 0.0f, H, M);
			TestEqual("3:00 hour hand", H, 90.0f, 1e-3f);
			TestEqual("3:00 minute hand", M, 0.0f, 1e-3f);
			CompanionSet::ClockAngles(0, 30, 0.0f, H, M);
			TestEqual("12:30 hour hand halfway to 1", H, 15.0f, 1e-3f);
			TestEqual("12:30 minute hand at 6", M, 180.0f, 1e-3f);
			CompanionSet::ClockAngles(23, 59, 30.0f, H, M);
			TestEqual("11:59:30 minute hand", M, 357.0f, 1e-3f);
			TestTrue("11:59:30 hour hand just short of 12", H > 359.0f && H < 360.0f);
		});
	});

	Describe("the path", [this]()
	{
		It("passes through every key and eases from rest to rest", [this]()
		{
			const TArray<FVector> P = { FVector(0, 0, 0), FVector(100, 50, 0), FVector(250, 60, 10), FVector(400, 0, 0) };
			for (int32 i = 0; i < P.Num(); ++i)
			{
				TestTrue(FString::Printf(TEXT("key %d"), i), FCompanionStagePath::CentripetalCatmullRom(P, (float)i).Equals(P[i], 0.01));
			}
			TestEqual("S-curve starts at 0", FCompanionStagePath::SCurve(0.0f, 0.3f, 0.5f), 0.0f);
			TestEqual("S-curve ends at 1", FCompanionStagePath::SCurve(1.0f, 0.3f, 0.5f), 1.0f, 1e-6f);
			// Peak speed is the mean over (1 - 0.15 - 0.25): 1.667 x mean in the cruise.
			const float Du = 1e-3f;
			const float Cruise = (FCompanionStagePath::SCurve(0.4f + Du, 0.3f, 0.5f) - FCompanionStagePath::SCurve(0.4f, 0.3f, 0.5f)) / Du;
			TestEqual("cruise speed", Cruise, 1.0f / 0.6f, 0.01f);
			const float Start = (FCompanionStagePath::SCurve(Du, 0.3f, 0.5f) - FCompanionStagePath::SCurve(0.0f, 0.3f, 0.5f)) / Du;
			TestTrue("starts from rest", Start < 0.01f);
		});
	});
}

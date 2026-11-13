#include "Misc/AutomationTest.h"
#include "Avatar/CompanionStageDirector.h"
#include "Avatar/CompanionStagePath.h"
#include "Settings/CompanionStageSettings.h"
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

	// Her home mark in the stage design: (650, -132), facing the lens down -X, eyes at 0.93 x stature.
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
		It("plays on first load when nothing is configured", [this, MakeLookup]()
		{
			TMap<FString, FString> Env;
			TestTrue("opening tour by default", UCompanionStageDirector::ShouldPlayOpeningShot(MakeLookup(Env)));
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
				float Prev = CompanionStage::ResidentYawOffsetAt(Plan, 0.0f);
				TestEqual("at work: turned 30 degrees away", Prev, 30.0f, 0.01f);
				bool bOneWay = true;
				for (float T = 0.0f; T <= Plan.MoveSeconds; T += 1.0f / 60.0f)
				{
					const float Offset = CompanionStage::ResidentYawOffsetAt(Plan, T);
					bOneWay &= Offset <= Prev + 1e-4f && FMath::Abs(Offset) <= 30.0f;
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

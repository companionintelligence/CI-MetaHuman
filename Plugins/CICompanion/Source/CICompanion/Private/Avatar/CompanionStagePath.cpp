#include "Avatar/CompanionStagePath.h"

#include "Settings/CompanionStageSettings.h"

namespace
{
	// Every helper here is prefixed: Unreal's unity build merges anonymous namespaces across the
	// .cpp files it concatenates, so a bare name can collide with a neighbour's.

	// Integral of smootherstep (6x^5 - 15x^4 + 10x^3) from 0 to X: the distance covered while the
	// S-curve's velocity ramps.
	double StagePathSmootherIntegral(double X)
	{
		X = FMath::Clamp(X, 0.0, 1.0);
		return X * X * X * X * (X * (X - 3.0) + 2.5);
	}

	// Arc-length table resolution. The design asks for 64 or more samples per segment; at 1024 the
	// table's own steps (the speed changes slightly from one cell to the next) stay under 2 cm/s^2 of
	// apparent acceleration when the move is sampled at 240 Hz, against a budget of 100.
	constexpr int32 StagePathSamplesPerSegment = 1024;

	bool StagePathParseSeconds(const FString& Raw, float& Out)
	{
		const FString Value = Raw.TrimStartAndEnd();
		if (Value.IsEmpty() || !Value.IsNumeric())
		{
			return false;
		}
		Out = FCString::Atof(*Value);
		return true;
	}

	void StagePathClampFractions(float& Accel, float& Decel)
	{
		Accel = FMath::Clamp(Accel, 0.0f, 1.0f);
		Decel = FMath::Clamp(Decel, 0.0f, 1.0f);
		if (Accel + Decel > 1.0f)
		{
			const float Scale = 1.0f / (Accel + Decel);
			Accel *= Scale;
			Decel *= Scale;
		}
	}

	// Samples a planned move at Rate Hz and fills the plan's measured comfort numbers. Returns the
	// time-to-contact at a 1-second duration (it scales linearly with the duration).
	void StagePathMeasure(FCompanionStagePlan& Plan, float HomeRangeCm, float Rate)
	{
		const FCompanionStagePath& Path = Plan.Opening;
		const float T = Path.GetSeconds();
		const int32 N = FMath::Max(2, FMath::CeilToInt(T * Rate));
		float Peak = 0.0f;
		float MinTtc = TNumericLimits<float>::Max();
		float MinDist = TNumericLimits<float>::Max();
		FVector Prev = Path.PositionAt(0.0f);
		float PrevDist = FVector::Dist(Prev, Plan.Aim);
		MinDist = PrevDist;
		for (int32 i = 1; i <= N; ++i)
		{
			const float Time = T * i / N;
			const FVector P = Path.PositionAt(Time);
			const float Dt = T / N;
			const float Speed = FVector::Dist(P, Prev) / Dt;
			Peak = FMath::Max(Peak, Speed);
			const float Dist = FVector::Dist(P, Plan.Aim);
			MinDist = FMath::Min(MinDist, Dist);
			const float Closing = (PrevDist - Dist) / Dt;
			if (Closing > 1e-3f && Dist > HomeRangeCm + 1.0f)
			{
				MinTtc = FMath::Min(MinTtc, Dist / Closing);
			}
			Prev = P;
			PrevDist = Dist;
		}
		Plan.PeakSpeedCmPerSecond = Peak;
		Plan.MinTimeToContactSeconds = MinTtc;
		Plan.MinEyeDistanceCm = MinDist;
	}
}

// ---------------------------------------------------------------------------------------- path

float FCompanionStagePath::SCurve(float U, float InAccel, float InDecel)
{
	float Accel = InAccel;
	float Decel = InDecel;
	StagePathClampFractions(Accel, Decel);
	const double A = Accel;
	const double D = Decel;
	const double X = FMath::Clamp((double)U, 0.0, 1.0);
	const double Total = 1.0 - 0.5 * A - 0.5 * D;
	double E = 0.0;
	if (A > 0.0 && X < A)
	{
		E = A * StagePathSmootherIntegral(X / A);
	}
	else if (X <= 1.0 - D)
	{
		E = 0.5 * A + (X - A);
	}
	else
	{
		E = 0.5 * A + (1.0 - A - D) + D * (0.5 - StagePathSmootherIntegral((1.0 - X) / D));
	}
	return (float)FMath::Clamp(E / Total, 0.0, 1.0);
}

FVector FCompanionStagePath::CentripetalCatmullRom(const TArray<FVector>& P, float U)
{
	const int32 N = P.Num();
	if (N == 0)
	{
		return FVector::ZeroVector;
	}
	if (N == 1)
	{
		return P[0];
	}
	if (N == 2)
	{
		return FMath::Lerp(P[0], P[1], (double)FMath::Clamp(U, 0.0f, 1.0f));
	}
	const int32 I = FMath::Clamp(FMath::FloorToInt(U), 0, N - 2);
	const double S = FMath::Clamp((double)U - I, 0.0, 1.0);
	auto At = [&P, N](int32 K) -> FVector
	{
		// Mirrored phantoms: the first and last segments neither overshoot nor stall.
		if (K < 0)
		{
			return 2.0 * P[0] - P[1];
		}
		if (K >= N)
		{
			return 2.0 * P[N - 1] - P[N - 2];
		}
		return P[K];
	};
	const FVector P0 = At(I - 1);
	const FVector P1 = At(I);
	const FVector P2 = At(I + 1);
	const FVector P3 = At(I + 2);
	auto Knot = [](double T, const FVector& A, const FVector& B)
	{
		return T + FMath::Sqrt(FMath::Max(FVector::Dist(A, B), 1e-3));
	};
	const double T0 = 0.0;
	const double T1 = Knot(T0, P0, P1);
	const double T2 = Knot(T1, P1, P2);
	const double T3 = Knot(T2, P2, P3);
	const double T = T1 + (T2 - T1) * S;
	const FVector A1 = ((T1 - T) / (T1 - T0)) * P0 + ((T - T0) / (T1 - T0)) * P1;
	const FVector A2 = ((T2 - T) / (T2 - T1)) * P1 + ((T - T1) / (T2 - T1)) * P2;
	const FVector A3 = ((T3 - T) / (T3 - T2)) * P2 + ((T - T2) / (T3 - T2)) * P3;
	const FVector B1 = ((T2 - T) / (T2 - T0)) * A1 + ((T - T0) / (T2 - T0)) * A2;
	const FVector B2 = ((T3 - T) / (T3 - T1)) * A2 + ((T - T1) / (T3 - T1)) * A3;
	return ((T2 - T) / (T2 - T1)) * B1 + ((T - T1) / (T2 - T1)) * B2;
}

void FCompanionStagePath::Build(const TArray<FVector>& InPoints, const TArray<FVector>& InLookAts,
	const TArray<FVector2D>& InScreens, float InSeconds, float InAccelFrac, float InDecelFrac)
{
	Points = InPoints;
	LookAts = InLookAts;
	Screens = InScreens;
	if (LookAts.Num() != Points.Num())
	{
		// No aim given: look ahead along the move, at the last key.
		LookAts.Init(Points.Num() > 0 ? Points.Last() : FVector::ZeroVector, Points.Num());
	}
	Seconds = FMath::Max(InSeconds, 0.0f);
	AccelFrac = InAccelFrac;
	DecelFrac = InDecelFrac;
	StagePathClampFractions(AccelFrac, DecelFrac);

	bConstantLookAt = true;
	for (const FVector& L : LookAts)
	{
		if (!L.Equals(LookAts[0], 0.01))
		{
			bConstantLookAt = false;
			break;
		}
	}

	TableParam.Reset();
	TableLength.Reset();
	KeyProgress.Reset();
	Length = 0.0f;
	const int32 N = Points.Num();
	if (N < 2)
	{
		KeyProgress.Init(0.0f, N);
		return;
	}
	const int32 Total = (N - 1) * StagePathSamplesPerSegment;
	TableParam.Reserve(Total + 1);
	TableLength.Reserve(Total + 1);
	TableParam.Add(0.0f);
	TableLength.Add(0.0f);
	FVector Prev = Points[0];
	double Acc = 0.0;
	for (int32 j = 1; j <= Total; ++j)
	{
		const float U = (float)j / StagePathSamplesPerSegment;
		const FVector P = CentripetalCatmullRom(Points, U);
		Acc += FVector::Dist(P, Prev);
		TableParam.Add(U);
		TableLength.Add((float)Acc);
		Prev = P;
	}
	Length = (float)Acc;
	for (int32 i = 0; i < N; ++i)
	{
		KeyProgress.Add(Length > KINDA_SMALL_NUMBER ? TableLength[i * StagePathSamplesPerSegment] / Length : 0.0f);
	}
}

float FCompanionStagePath::ParamAtDistance(float Distance) const
{
	if (TableLength.Num() < 2)
	{
		return 0.0f;
	}
	const float S = FMath::Clamp(Distance, 0.0f, Length);
	int32 Lo = 0;
	int32 Hi = TableLength.Num() - 1;
	while (Hi - Lo > 1)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (TableLength[Mid] < S)
		{
			Lo = Mid;
		}
		else
		{
			Hi = Mid;
		}
	}
	const float Span = TableLength[Hi] - TableLength[Lo];
	const float F = Span > KINDA_SMALL_NUMBER ? (S - TableLength[Lo]) / Span : 0.0f;
	return FMath::Lerp(TableParam[Lo], TableParam[Hi], F);
}

float FCompanionStagePath::ProgressAt(float Time) const
{
	if (!IsMoving())
	{
		return Time > 0.0f && Points.Num() > 1 ? 1.0f : 0.0f;
	}
	return SCurve(Time / Seconds, AccelFrac, DecelFrac);
}

FVector FCompanionStagePath::PositionAtProgress(float Progress) const
{
	if (Points.Num() == 0)
	{
		return FVector::ZeroVector;
	}
	if (Points.Num() == 1 || Length <= KINDA_SMALL_NUMBER)
	{
		return Progress >= 1.0f ? Points.Last() : Points[0];
	}
	return CentripetalCatmullRom(Points, ParamAtDistance(FMath::Clamp(Progress, 0.0f, 1.0f) * Length));
}

FVector FCompanionStagePath::LookAtAtProgress(float Progress) const
{
	if (LookAts.Num() == 0)
	{
		return FVector::ZeroVector;
	}
	if (bConstantLookAt || LookAts.Num() == 1 || Length <= KINDA_SMALL_NUMBER)
	{
		return Progress >= 1.0f && !bConstantLookAt ? LookAts.Last() : LookAts[0];
	}
	// The SAME curve parameter as the position, so aim and position move together and the aim
	// cannot snap at a key.
	return CentripetalCatmullRom(LookAts, ParamAtDistance(FMath::Clamp(Progress, 0.0f, 1.0f) * Length));
}

FVector FCompanionStagePath::PositionAt(float Time) const
{
	return PositionAtProgress(ProgressAt(Time));
}

FVector FCompanionStagePath::LookAtAt(float Time) const
{
	return LookAtAtProgress(ProgressAt(Time));
}

FVector2D FCompanionStagePath::ScreenAt(float Time) const
{
	if (Screens.Num() == 0)
	{
		return FVector2D(0.5, 0.5);
	}
	if (Screens.Num() == 1 || KeyProgress.Num() != Screens.Num())
	{
		return Screens[0];
	}
	const float P = ProgressAt(Time);
	if (P <= KeyProgress[0])
	{
		return Screens[0];
	}
	for (int32 i = 1; i < Screens.Num(); ++i)
	{
		if (P <= KeyProgress[i])
		{
			const float Span = FMath::Max(KeyProgress[i] - KeyProgress[i - 1], 1e-6f);
			return FMath::Lerp(Screens[i - 1], Screens[i], (double)((P - KeyProgress[i - 1]) / Span));
		}
	}
	return Screens.Last();
}

// ---------------------------------------------------------------------------------------- plans

void CompanionStage::FallbackKeys(const FVector& Subject, float EyeZ, TArray<FVector>& OutPositions, TArray<FVector>& OutLookAts)
{
	// The path the director flew before the stage layout existed: from outside the old glass
	// frontage, down the aisle, past the tape and disk rows, arcing into a roomy call frame 430 cm
	// from her. Kept verbatim as the fallback for a project with no stage section in its config.
	const float FinalDist = 430.0f;
	OutPositions = {
		FVector(Subject.X - 1550.0f, Subject.Y - 220.0f, 280.0f),
		FVector(Subject.X - 760.0f, Subject.Y - 240.0f, 255.0f),
		FVector(Subject.X + 80.0f, Subject.Y - 560.0f, 230.0f),
		FVector(Subject.X + 720.0f, Subject.Y + 480.0f, 220.0f),
		FVector(Subject.X + 400.0f, Subject.Y + 220.0f, 185.0f),
		FVector(Subject.X - FinalDist, Subject.Y, EyeZ - 8.0f)};
	OutLookAts = {
		FVector(Subject.X + 450.0f, Subject.Y - 80.0f, 185.0f),
		FVector(Subject.X + 220.0f, Subject.Y - 120.0f, 165.0f),
		FVector(Subject.X + 360.0f, Subject.Y - 250.0f, 145.0f),
		FVector(Subject.X + 390.0f, Subject.Y + 250.0f, 145.0f),
		FVector(Subject.X + 90.0f, Subject.Y + 40.0f, EyeZ - 25.0f),
		FVector(Subject.X, Subject.Y - 8.0f, EyeZ - 18.0f)};
}

FCompanionStagePlan CompanionStage::BuildFallbackPlan(const FCompanionStageSubject& Subject, TFunctionRef<FString(const TCHAR*)> Env)
{
	FCompanionStagePlan Plan;
	Plan.bFromConfig = false;
	Plan.bLevel = false;
	TArray<FVector> Points;
	TArray<FVector> LookAts;
	FallbackKeys(Subject.Mark, Subject.EyeZ, Points, LookAts);

	float Fly = 11.0f;
	float Parsed = 0.0f;
	if (StagePathParseSeconds(Env(TEXT("MHLLM_FLY_SECONDS")), Parsed) && Parsed > 0.0f)
	{
		Fly = Parsed;
	}
	Plan.Opening.Build(Points, LookAts, TArray<FVector2D>(), Fly, 0.3f, 0.5f);
	Plan.MoveSeconds = Fly;
	// FOV 48 degrees horizontal, the lens this path was framed with: 18 / tan(24 deg) = 40.4 mm.
	Plan.FocalMm = 18.0f / FMath::Tan(FMath::DegreesToRadians(24.0f));
	Plan.FilmbackMm = FVector2D(36.0, 20.25);
	Plan.Aperture = 0.0f;
	Plan.Aim = LookAts.Last();
	Plan.StatureCm = FMath::Max(Subject.EyeZ - Subject.Mark.Z, 1.0f) / 0.93f;

	auto Hold = [&](const FVector& Location, const FVector& LookAt)
	{
		FCompanionStageFraming F;
		F.bValid = true;
		F.Location = Location;
		F.LookAt = LookAt;
		F.FocalMm = Plan.FocalMm;
		F.FilmbackMm = Plan.FilmbackMm;
		F.Aperture = 0.0f;
		return F;
	};
	Plan.Lobby = Hold(Points[0], LookAts[0]);
	Plan.Home = Hold(Points.Last(), LookAts.Last());
	Plan.bCutIntoMove = false;

	Plan.HoldSeconds = 3.0f;
	if (StagePathParseSeconds(Env(TEXT("MHLLM_OPENING_DELAY")), Parsed) && Parsed >= 0.0f)
	{
		Plan.HoldSeconds = Parsed;
	}
	// She waits with her back to the camera and turns through 180 once it has landed.
	Plan.TurnStart = Fly;
	Plan.TurnEnd = Fly + 1.6f;
	Plan.WorkTurnDegrees = 180.0f;
	Plan.LookUpAt = Fly;
	Plan.SettleAt = Fly;
	Plan.SpeakAt = Plan.TurnEnd + 0.35f;
	Plan.GreetingAt = -1.0f;
	Plan.StaticGreetingAt = -1.0f;
	Plan.StaticSpeakAt = 0.35f;
	StagePathMeasure(Plan, 0.0f, 240.0f);
	return Plan;
}

FCompanionStagePlan CompanionStage::BuildPlan(const UCompanionStageSettings* Settings, const FCompanionStageSubject& Subject,
	TFunctionRef<FString(const TCHAR*)> Env, bool bPortrait)
{
	if (!Settings || !Settings->HasOpening())
	{
		FCompanionStagePlan Fallback = BuildFallbackPlan(Subject, Env);
		Fallback.Notes.Add(TEXT("no stage shots in [/Script/CICompanion.CompanionStageSettings]; flying the built-in path"));
		return Fallback;
	}

	FCompanionStagePlan Plan;
	Plan.bFromConfig = true;
	Plan.bLevel = true;

	// Her stature from her measured eyes, and everything near her from it.
	const float Ref = FMath::Max(Settings->ReferenceStatureCm, 1.0f);
	const float EyeFraction = FMath::Clamp(Settings->EyeFraction, 0.5f, 1.0f);
	Plan.StatureCm = FMath::Max((Subject.EyeZ - Subject.Mark.Z) / EyeFraction, 50.0f);
	const float K = Plan.StatureCm / Ref;
	const float Facing = Subject.FacingYaw;
	const float FacingRad = FMath::DegreesToRadians(Facing);
	const FVector E(Subject.Mark.X + Settings->EyeForwardCm * K * FMath::Cos(FacingRad),
		Subject.Mark.Y + Settings->EyeForwardCm * K * FMath::Sin(FacingRad),
		Subject.EyeZ);
	Plan.Aim = E;

	auto KeyLocation = [&E, Facing](float RangeCm, float ThetaDeg, float DzCm)
	{
		const float A = FMath::DegreesToRadians(Facing - ThetaDeg);
		return FVector(E.X + RangeCm * FMath::Cos(A), E.Y + RangeCm * FMath::Sin(A), E.Z + DzCm);
	};
	// Resident-frame ranges scale with her. A shot's start may not stand farther out than its
	// MaxRangeCm (the move starts inside the glass): the radial excess is scaled down evenly, so the
	// keys stay evenly spaced in range.
	auto ScaledRanges = [&Plan, K](const FCompanionStageShot& Shot)
	{
		TArray<float> R;
		for (const FCompanionStageKey& Key : Shot.Keys)
		{
			R.Add(Key.RangeCm * K);
		}
		if (Shot.MaxRangeCm > 0.0f && R.Num() >= 2 && R[0] > Shot.MaxRangeCm)
		{
			const float RHome = R.Last();
			const float Span = R[0] - RHome;
			if (Span > KINDA_SMALL_NUMBER)
			{
				const float Scale = FMath::Max(Shot.MaxRangeCm - RHome, 0.0f) / Span;
				Plan.Notes.Add(FString::Printf(TEXT("%s starts %.0f cm from her eyes (capped from %.0f)"),
					*Shot.Name.ToString(), RHome + Span * Scale, R[0]));
				for (float& V : R)
				{
					V = RHome + (V - RHome) * Scale;
				}
			}
		}
		return R;
	};
	auto Framing = [&](const FCompanionStageShot* Shot)
	{
		FCompanionStageFraming F;
		if (!Shot || Shot->Keys.Num() == 0)
		{
			return F;
		}
		F.bValid = true;
		F.FocalMm = Shot->FocalMm;
		F.FilmbackMm = Shot->FilmbackMm;
		F.Aperture = Shot->Aperture;
		const FCompanionStageKey& Key = Shot->Keys[0];
		if (Shot->Frame == ECompanionStageFrame::World)
		{
			F.Location = Key.Location;
			F.LookAt = Key.LookAt;
			F.ShiftMm = Shot->ShiftMm;
		}
		else
		{
			const TArray<float> R = ScaledRanges(*Shot);
			F.Location = KeyLocation(R[0], Key.ThetaDeg, Key.DzCm * K);
			F.LookAt = E;
			F.bHoldScreen = true;
			F.Screen = Key.Screen;
		}
		return F;
	};

	const FCompanionStageShot* Opening = Settings->FindShotFor(Settings->OpeningShot, bPortrait);
	Plan.Home = Framing(Settings->FindShotFor(Settings->HomeShot, bPortrait));
	Plan.Close = Framing(Settings->FindShotFor(Settings->CloseShot, bPortrait));

	// The move: every key resolved against her, the aim held on her eyes, her eyes composed by key.
	TArray<FVector> Points;
	TArray<FVector> LookAts;
	TArray<FVector2D> Screens;
	const TArray<float> R = ScaledRanges(*Opening);
	for (int32 i = 0; i < Opening->Keys.Num(); ++i)
	{
		const FCompanionStageKey& Key = Opening->Keys[i];
		if (Opening->Frame == ECompanionStageFrame::World)
		{
			Points.Add(Key.Location);
			LookAts.Add(Key.LookAt);
		}
		else
		{
			Points.Add(KeyLocation(R[i], Key.ThetaDeg, Key.DzCm * K));
			LookAts.Add(E);
			Screens.Add(Key.Screen);
		}
	}
	Plan.FocalMm = Opening->FocalMm;
	Plan.FilmbackMm = Opening->FilmbackMm;
	Plan.Aperture = Opening->Aperture;
	Plan.Opening.Build(Points, LookAts, Screens, FMath::Max(Opening->Seconds, 0.1f), Opening->AccelFrac, Opening->DecelFrac);

	Plan.Lobby = Framing(Settings->FindShotFor(Settings->LobbyShot, bPortrait));
	if (!Plan.Lobby.bValid)
	{
		Plan.Lobby.bValid = true;
		Plan.Lobby.Location = Points[0];
		Plan.Lobby.LookAt = LookAts[0];
		Plan.Lobby.FocalMm = Plan.FocalMm;
		Plan.Lobby.FilmbackMm = Plan.FilmbackMm;
		Plan.Lobby.Aperture = Plan.Aperture;
		Plan.Lobby.bHoldScreen = Screens.Num() > 0;
		Plan.Lobby.Screen = Screens.Num() > 0 ? Screens[0] : FVector2D(0.5, 0.5);
	}
	Plan.bCutIntoMove = !Plan.Lobby.Location.Equals(Points[0], 1.0);

	// THE DURATION, from the comfort budget for this resident (design 6.3): peak speed at most
	// vmax, which scales down for a smaller resident; the camera's turn well inside HFOV/7 per
	// second; at least MinMoveSeconds; then lengthened until the time to contact holds.
	float Accel = Plan.Opening.GetAccelFrac();
	float Decel = Plan.Opening.GetDecelFrac();
	const float PeakFactor = 1.0f / FMath::Max(1.0f - 0.5f * Accel - 0.5f * Decel, 0.1f);
	const float Vmax = FMath::Max(Settings->MaxSpeedMetersPerSecond, 0.05f) * 100.0f * FMath::Min(1.0f, K);
	const float HFov = FMath::RadiansToDegrees(2.0f * FMath::Atan(Plan.FilmbackMm.X / (2.0f * FMath::Max(Plan.FocalMm, 1.0f))));
	const float YawBudget = HFov / 7.0f;
	float BearingChange = 0.0f;
	if (Opening->Frame == ECompanionStageFrame::Resident && Opening->Keys.Num() >= 2)
	{
		BearingChange = FMath::Abs(Opening->Keys[0].ThetaDeg - Opening->Keys.Last().ThetaDeg);
	}
	const float Floor = FMath::Max(PeakFactor * Plan.Opening.GetLength() / Vmax,
		PeakFactor * BearingChange / (0.9f * FMath::Max(YawBudget, 0.1f)));
	float T = FMath::Max(Floor, Settings->MinMoveSeconds);
	if (Opening->Frame == ECompanionStageFrame::Resident)
	{
		// Time to contact scales linearly with the duration, so measure it once at 1 s.
		Plan.Opening.SetSeconds(1.0f);
		StagePathMeasure(Plan, R.Last(), 800.0f);
		const float TtcAtOneSecond = Plan.MinTimeToContactSeconds;
		if (TtcAtOneSecond > 0.0f && TtcAtOneSecond < TNumericLimits<float>::Max() && TtcAtOneSecond * T < Settings->MinTimeToContactSeconds)
		{
			const float Needed = Settings->MinTimeToContactSeconds / TtcAtOneSecond;
			T += FMath::CeilToFloat((Needed - T) / 0.05f - KINDA_SMALL_NUMBER) * 0.05f;
		}
	}
	float Cap = Settings->MaxMoveSeconds;
	bool bCapFromEnv = false;
	float Parsed = 0.0f;
	if (StagePathParseSeconds(Env(TEXT("MHLLM_FLY_SECONDS")), Parsed) && Parsed > 0.0f)
	{
		Cap = Parsed;
		bCapFromEnv = true;
	}
	if (Cap < Floor)
	{
		Plan.Notes.Add(FString::Printf(TEXT("%s=%.2f s is below the comfort floor (%.2f s) and was ignored"),
			bCapFromEnv ? TEXT("MHLLM_FLY_SECONDS") : TEXT("MaxMoveSeconds"), Cap, Floor));
	}
	else if (T > Cap)
	{
		Plan.Notes.Add(FString::Printf(TEXT("move capped at %.2f s (%s)"), Cap,
			bCapFromEnv ? TEXT("MHLLM_FLY_SECONDS") : TEXT("MaxMoveSeconds")));
		T = Cap;
	}
	Plan.MoveSeconds = T;
	Plan.Opening.SetSeconds(T);
	StagePathMeasure(Plan, R.Last(), 240.0f);

	// THE BEATS, solved on the planned move: she looks up when the camera comes within the look-up
	// distance, her body turns back from work just after, the move settles when it drops under the
	// settle speed, and the turn-yield line follows the settle (never before the move ends).
	const int32 Steps = FMath::Max(2, FMath::CeilToInt(T * 480.0f));
	const float LookUpDistance = Settings->LookUpDistanceCm * K;
	Plan.LookUpAt = -1.0f;
	Plan.SettleAt = -1.0f;
	FVector Prev = Plan.Opening.PositionAt(0.0f);
	for (int32 i = 1; i <= Steps; ++i)
	{
		const float Time = T * i / Steps;
		const FVector P = Plan.Opening.PositionAt(Time);
		if (Plan.LookUpAt < 0.0f && FVector::Dist(P, E) <= LookUpDistance)
		{
			Plan.LookUpAt = Time;
		}
		const float Speed = FVector::Dist(P, Prev) / (T / Steps);
		if (Plan.SettleAt < 0.0f && Time > 0.6f * T && Speed < Settings->SettleSpeedCmPerSecond)
		{
			Plan.SettleAt = Time;
		}
		Prev = P;
	}
	if (Plan.LookUpAt < 0.0f)
	{
		Plan.LookUpAt = 0.45f * T;
	}
	if (Plan.SettleAt < 0.0f)
	{
		Plan.SettleAt = T;
	}
	Plan.WorkTurnDegrees = FMath::Clamp(Settings->WorkTurnDegrees, -30.0f, 30.0f);
	Plan.TurnStart = Plan.LookUpAt + FMath::Max(Settings->TurnDelaySeconds, 0.0f);
	Plan.TurnEnd = Plan.TurnStart + FMath::Max(Settings->TurnSeconds, 0.05f);
	Plan.SpeakAt = FMath::Max(Plan.SettleAt + FMath::Max(Settings->SpeakAfterSettleSeconds, 0.0f), T);
	const bool bGreeting = !Settings->GreetingLine.TrimStartAndEnd().IsEmpty();
	Plan.GreetingLine = Settings->GreetingLine;
	Plan.GreetingAt = bGreeting && Settings->GreetingAtSeconds >= 0.0f ? Settings->GreetingAtSeconds : -1.0f;
	Plan.StaticGreetingAt = bGreeting ? Settings->ReducedMotionGreetingSeconds : -1.0f;
	Plan.StaticSpeakAt = FMath::Max(Settings->ReducedMotionSpeakSeconds, 0.0f);

	Plan.HoldSeconds = FMath::Max(Settings->HoldSeconds, 0.0f);
	if (StagePathParseSeconds(Env(TEXT("MHLLM_OPENING_DELAY")), Parsed) && Parsed >= 0.0f)
	{
		Plan.HoldSeconds = Parsed;
	}
	return Plan;
}

float CompanionStage::ResidentYawOffsetAt(const FCompanionStagePlan& Plan, float MoveTime)
{
	if (FMath::IsNearlyZero(Plan.WorkTurnDegrees) || MoveTime >= Plan.TurnEnd)
	{
		return MoveTime >= Plan.TurnEnd ? 0.0f : Plan.WorkTurnDegrees;
	}
	if (MoveTime <= Plan.TurnStart)
	{
		return Plan.WorkTurnDegrees;
	}
	const float U = FMath::Clamp((MoveTime - Plan.TurnStart) / FMath::Max(Plan.TurnEnd - Plan.TurnStart, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
	return Plan.WorkTurnDegrees * (1.0f - U * U * (3.0f - 2.0f * U));
}

FVector2D CompanionStage::SensorShiftFor(const FVector& Location, const FVector& Aim, const FVector2D& Screen,
	float FocalMm, const FVector2D& FilmbackMm)
{
	const FVector D = Aim - Location;
	const double Horizontal = FMath::Max(FVector2D(D.X, D.Y).Size(), 1.0);
	// Where her eyes land on the unshifted, level sensor: on the vertical centre line (the camera
	// is yawed at them), and FocalMm * rise / run above the centre.
	const double EyeUpMm = FocalMm * D.Z / Horizontal;
	return FVector2D(-(Screen.X - 0.5) * FilmbackMm.X, EyeUpMm - (0.5 - Screen.Y) * FilmbackMm.Y);
}

FRotator CompanionStage::AimRotation(const FVector& Location, const FVector& LookAt, bool bLevel)
{
	const FVector D = LookAt - Location;
	if (bLevel)
	{
		return FRotator(0.0, FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)), 0.0);
	}
	return D.Rotation();
}

FRotator CompanionStage::BreathOffset(float Seconds, float AmplitudeDegrees)
{
	if (AmplitudeDegrees <= 0.0f)
	{
		return FRotator::ZeroRotator;
	}
	// Weights sum to 1 per axis, so |yaw| <= A and |pitch| <= 0.6 A. Peak rate is
	// 2 pi A (0.62 x 0.071 + 0.38 x 0.113) = 0.55 A deg/s for yaw; a held shot drifts a few
	// pixels, never visibly swings.
	const double T = Seconds;
	const double TwoPi = 2.0 * UE_DOUBLE_PI;
	const double Yaw = AmplitudeDegrees * (0.62 * FMath::Sin(TwoPi * 0.071 * T) + 0.38 * FMath::Sin(TwoPi * 0.113 * T + 1.3));
	const double Pitch = 0.6 * AmplitudeDegrees * (0.55 * FMath::Sin(TwoPi * 0.089 * T + 0.4) + 0.45 * FMath::Sin(TwoPi * 0.137 * T + 2.1));
	return FRotator(Pitch, Yaw, 0.0);
}

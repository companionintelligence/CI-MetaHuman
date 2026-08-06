#include "Avatar/FaceProfile.h"

namespace
{
	void Add(FFaceProfile& Profile, EViseme Viseme, std::initializer_list<FFaceCurve> Curves)
	{
		FVisemeShape Shape;
		Shape.Curves.Append(Curves);
		Profile.Shapes.Add(Viseme, MoveTemp(Shape));
	}

	// Reallusion CC ships exactly the 15 V_* viseme morphs, so most classes map close to 1:1.
	// The jaw is added separately and deliberately under-driven: the V_* shapes already imply
	// some aperture, and stacking a full Jaw_Open on top reads as a puppet hinge.
	void AddEmotion(FFaceProfile& P, EEmotionType E, std::initializer_list<FFaceCurve> Curves)
	{
		FVisemeShape Shape;
		for (const FFaceCurve& C : Curves) { Shape.Curves.Add(C); }
		P.EmotionShapes.Add(E, Shape);
	}

	FFaceProfile BuildCC()
	{
		FFaceProfile P;
		P.ProfileName = TEXT("CC");
		P.BlinkCurves = { TEXT("Eye_Blink_L"), TEXT("Eye_Blink_R") };
		P.GazeCurves  = { TEXT("Eye_L_Look_L"), TEXT("Eye_L_Look_R"), TEXT("Eye_L_Look_Up"), TEXT("Eye_L_Look_Down"),
						  TEXT("Eye_R_Look_L"), TEXT("Eye_R_Look_R"), TEXT("Eye_R_Look_Up"), TEXT("Eye_R_Look_Down") };
		P.EyeWidenCurves = { TEXT("Eye_Wide_L"), TEXT("Eye_Wide_R") };
		// Weights stay LOW (<=0.45). A full-strength FACS pose reads as a caricature, and these sit
		// under speech continuously rather than flashing, so anything stronger looks unhinged.
		AddEmotion(P, EEmotionType::Neutral, {});
		AddEmotion(P, EEmotionType::Happy, { { TEXT("Mouth_Smile_L"), 0.35f }, { TEXT("Mouth_Smile_R"), 0.35f },
											 { TEXT("Cheek_Raise_L"), 0.30f }, { TEXT("Cheek_Raise_R"), 0.30f },
											 { TEXT("Brow_Raise_Outer_L"), 0.15f }, { TEXT("Brow_Raise_Outer_R"), 0.15f } });
		AddEmotion(P, EEmotionType::Angry, { { TEXT("Brow_Drop_L"), 0.45f }, { TEXT("Brow_Drop_R"), 0.45f },
											 { TEXT("Brow_Compress_L"), 0.35f }, { TEXT("Brow_Compress_R"), 0.35f },
											 { TEXT("Eye_Squint_L"), 0.20f }, { TEXT("Eye_Squint_R"), 0.20f } });
		AddEmotion(P, EEmotionType::Sad,   { { TEXT("Brow_Raise_Inner_L"), 0.40f }, { TEXT("Brow_Raise_Inner_R"), 0.40f },
											 { TEXT("Mouth_Frown_L"), 0.25f }, { TEXT("Mouth_Frown_R"), 0.25f } });
		AddEmotion(P, EEmotionType::Fear,  { { TEXT("Eye_Wide_L"), 0.40f }, { TEXT("Eye_Wide_R"), 0.40f },
											 { TEXT("Brow_Raise_Inner_L"), 0.35f }, { TEXT("Brow_Raise_Inner_R"), 0.35f },
											 { TEXT("Brow_Raise_Outer_L"), 0.25f }, { TEXT("Brow_Raise_Outer_R"), 0.25f } });
		AddEmotion(P, EEmotionType::Love,  { { TEXT("Mouth_Smile_L"), 0.25f }, { TEXT("Mouth_Smile_R"), 0.25f },
											 { TEXT("Cheek_Raise_L"), 0.20f }, { TEXT("Cheek_Raise_R"), 0.20f },
											 { TEXT("Eye_Squint_L"), 0.15f }, { TEXT("Eye_Squint_R"), 0.15f } });
		AddEmotion(P, EEmotionType::Alert, { { TEXT("Brow_Raise_Outer_L"), 0.35f }, { TEXT("Brow_Raise_Outer_R"), 0.35f },
											 { TEXT("Eye_Wide_L"), 0.25f }, { TEXT("Eye_Wide_R"), 0.25f } });
		Add(P, EViseme::Sil, { { TEXT("Mouth_Close"), 0.15f } });
		Add(P, EViseme::PP,  { { TEXT("V_Explosive"), 1.0f } });
		Add(P, EViseme::FF,  { { TEXT("V_Dental_Lip"), 1.0f } });
		Add(P, EViseme::TH,  { { TEXT("V_Tongue_Out"), 0.85f }, { TEXT("Jaw_Open"), 0.20f } });
		Add(P, EViseme::DD,  { { TEXT("V_Tongue_up"), 0.90f }, { TEXT("Jaw_Open"), 0.15f } });
		Add(P, EViseme::KK,  { { TEXT("V_Tongue_Lower"), 0.80f }, { TEXT("Jaw_Open"), 0.25f } });
		Add(P, EViseme::CH,  { { TEXT("V_Affricate"), 1.0f } });
		Add(P, EViseme::SS,  { { TEXT("V_Tight"), 0.90f } });
		Add(P, EViseme::NN,  { { TEXT("V_Tongue_Raise"), 0.85f } });
		Add(P, EViseme::RR,  { { TEXT("V_Tongue_Curl_U"), 0.85f }, { TEXT("V_Tight_O"), 0.30f } });
		Add(P, EViseme::AA,  { { TEXT("V_Open"), 1.0f }, { TEXT("Jaw_Open"), 0.55f } });
		Add(P, EViseme::E,   { { TEXT("V_Wide"), 0.80f }, { TEXT("Jaw_Open"), 0.30f } });
		Add(P, EViseme::IH,  { { TEXT("V_Wide"), 0.55f }, { TEXT("Jaw_Open"), 0.15f } });
		Add(P, EViseme::OH,  { { TEXT("V_Tight_O"), 0.80f }, { TEXT("Jaw_Open"), 0.35f } });
		Add(P, EViseme::OU,  { { TEXT("V_Tight_O"), 1.0f }, { TEXT("Jaw_Open"), 0.10f } });
		return P;
	}

	// Apple ARKit naming (MetaHuman). Kept so the same mapper drives Seo or any MetaHuman face;
	// ARKit has no tongue channels to speak of, so TH/DD/NN/RR lean on jaw + lip shapes.
	FFaceProfile BuildARKit()
	{
		FFaceProfile P;
		P.ProfileName = TEXT("ARKit");
		P.BlinkCurves = { TEXT("eyeBlinkLeft"), TEXT("eyeBlinkRight") };
		// In/Out swapped between the eyes ON PURPOSE — see FFaceProfile::GazeCurves. Index 0 and 4
		// must both mean "look left", and on ARKit that is Out for the left eye and In for the right.
		P.GazeCurves  = { TEXT("eyeLookOutLeft"), TEXT("eyeLookInLeft"), TEXT("eyeLookUpLeft"), TEXT("eyeLookDownLeft"),
						  TEXT("eyeLookInRight"), TEXT("eyeLookOutRight"), TEXT("eyeLookUpRight"), TEXT("eyeLookDownRight") };
		P.EyeWidenCurves = { TEXT("eyeWideLeft"), TEXT("eyeWideRight") };
		AddEmotion(P, EEmotionType::Neutral, {});
		AddEmotion(P, EEmotionType::Happy, { { TEXT("mouthSmileLeft"), 0.35f }, { TEXT("mouthSmileRight"), 0.35f },
											 { TEXT("cheekSquintLeft"), 0.30f }, { TEXT("cheekSquintRight"), 0.30f } });
		AddEmotion(P, EEmotionType::Angry, { { TEXT("browDownLeft"), 0.45f }, { TEXT("browDownRight"), 0.45f },
											 { TEXT("eyeSquintLeft"), 0.20f }, { TEXT("eyeSquintRight"), 0.20f } });
		AddEmotion(P, EEmotionType::Sad,   { { TEXT("browInnerUp"), 0.40f },
											 { TEXT("mouthFrownLeft"), 0.25f }, { TEXT("mouthFrownRight"), 0.25f } });
		AddEmotion(P, EEmotionType::Fear,  { { TEXT("eyeWideLeft"), 0.40f }, { TEXT("eyeWideRight"), 0.40f },
											 { TEXT("browInnerUp"), 0.35f } });
		AddEmotion(P, EEmotionType::Love,  { { TEXT("mouthSmileLeft"), 0.25f }, { TEXT("mouthSmileRight"), 0.25f },
											 { TEXT("cheekSquintLeft"), 0.20f }, { TEXT("cheekSquintRight"), 0.20f } });
		AddEmotion(P, EEmotionType::Alert, { { TEXT("browOuterUpLeft"), 0.35f }, { TEXT("browOuterUpRight"), 0.35f },
											 { TEXT("eyeWideLeft"), 0.25f }, { TEXT("eyeWideRight"), 0.25f } });
		Add(P, EViseme::Sil, { { TEXT("mouthClose"), 0.10f } });
		Add(P, EViseme::PP,  { { TEXT("mouthPressLeft"), 0.70f }, { TEXT("mouthPressRight"), 0.70f }, { TEXT("mouthClose"), 0.60f } });
		Add(P, EViseme::FF,  { { TEXT("mouthLowerDownLeft"), 0.40f }, { TEXT("mouthLowerDownRight"), 0.40f }, { TEXT("mouthShrugUpper"), 0.40f } });
		Add(P, EViseme::TH,  { { TEXT("tongueOut"), 0.60f }, { TEXT("jawOpen"), 0.20f } });
		Add(P, EViseme::DD,  { { TEXT("jawOpen"), 0.22f }, { TEXT("mouthShrugUpper"), 0.20f } });
		Add(P, EViseme::KK,  { { TEXT("jawOpen"), 0.28f } });
		Add(P, EViseme::CH,  { { TEXT("mouthFunnel"), 0.55f }, { TEXT("jawOpen"), 0.20f } });
		Add(P, EViseme::SS,  { { TEXT("mouthStretchLeft"), 0.35f }, { TEXT("mouthStretchRight"), 0.35f } });
		Add(P, EViseme::NN,  { { TEXT("jawOpen"), 0.15f }, { TEXT("mouthClose"), 0.30f } });
		Add(P, EViseme::RR,  { { TEXT("mouthFunnel"), 0.40f }, { TEXT("mouthPucker"), 0.30f } });
		Add(P, EViseme::AA,  { { TEXT("jawOpen"), 0.75f } });
		Add(P, EViseme::E,   { { TEXT("jawOpen"), 0.35f }, { TEXT("mouthSmileLeft"), 0.25f }, { TEXT("mouthSmileRight"), 0.25f } });
		Add(P, EViseme::IH,  { { TEXT("jawOpen"), 0.20f }, { TEXT("mouthSmileLeft"), 0.20f }, { TEXT("mouthSmileRight"), 0.20f } });
		Add(P, EViseme::OH,  { { TEXT("jawOpen"), 0.45f }, { TEXT("mouthFunnel"), 0.50f } });
		Add(P, EViseme::OU,  { { TEXT("mouthPucker"), 0.85f }, { TEXT("jawOpen"), 0.12f } });
		return P;
	}
}

void FFaceProfile::Accumulate(EViseme Viseme, float Weight, TMap<FName, float>& OutCurves) const
{
	if (const FVisemeShape* Shape = Shapes.Find(Viseme))
	{
		for (const FFaceCurve& Curve : Shape->Curves)
		{
			// Sum, then clamp: two overlapping visemes sharing Jaw_Open must not drive it past 1.
			float& Value = OutCurves.FindOrAdd(Curve.Curve, 0.0f);
			Value = FMath::Clamp(Value + (Curve.Scale * Weight), 0.0f, 1.0f);
		}
	}
}

void FFaceProfile::AccumulateEmotion(EEmotionType Emotion, float Weight, TMap<FName, float>& OutCurves) const
{
	if (const FVisemeShape* Shape = EmotionShapes.Find(Emotion))
	{
		for (const FFaceCurve& Curve : Shape->Curves)
		{
			OutCurves.FindOrAdd(Curve.Curve) += Curve.Scale * Weight;
		}
	}
}

TArray<FName> FFaceProfile::AllCurves() const
{
	TSet<FName> Unique;
	for (const TPair<EViseme, FVisemeShape>& Pair : Shapes)
	{
		for (const FFaceCurve& Curve : Pair.Value.Curves)
		{
			Unique.Add(Curve.Curve);
		}
	}
	// Blink curves too: AllCurves is what the driver zeroes each frame, so omitting them would
	// leave a blink latched shut the moment the eyes closed.
	for (const FName& Blink : BlinkCurves)
	{
		Unique.Add(Blink);
	}
	// Emotion and gaze curves too, for the same reason: AllCurves is the zeroing set, so anything
	// omitted here stays latched at whatever it was last set to.
	for (const TPair<EEmotionType, FVisemeShape>& Pair : EmotionShapes)
	{
		for (const FFaceCurve& Curve : Pair.Value.Curves) { Unique.Add(Curve.Curve); }
	}
	for (const FName& Gaze : GazeCurves)
	{
		Unique.Add(Gaze);
	}
	// Widen curves are already reachable through EmotionShapes on both shipped profiles, so this is
	// belt-and-braces — but AllCurves is the zeroing set, and a profile that ever listed a widen
	// curve no emotion used would otherwise latch it on permanently.
	for (const FName& Widen : EyeWidenCurves)
	{
		Unique.Add(Widen);
	}
	return Unique.Array();
}

const FFaceProfile& FFaceProfile::CharacterCreator()
{
	static const FFaceProfile Profile = BuildCC();
	return Profile;
}

const FFaceProfile& FFaceProfile::ARKit()
{
	static const FFaceProfile Profile = BuildARKit();
	return Profile;
}

const FFaceProfile& FFaceProfile::ByName(const FString& Name)
{
	return Name.Equals(TEXT("ARKit"), ESearchCase::IgnoreCase) ? ARKit() : CharacterCreator();
}

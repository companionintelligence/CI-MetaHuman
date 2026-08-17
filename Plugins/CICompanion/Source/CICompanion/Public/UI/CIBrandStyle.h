#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"

/**
 * Companion Intelligence brand tokens for Slate.
 *
 * Values are the DARK-mode token set from CI-Common's canonical style guide
 * (`CI-Common/docs/UI-STYLE-GUIDE.md` section 3.2, package `@companionintelligence/tokens`).
 * Dark is the correct basis here and not a preference: this HUD is composited over a dimly-lit
 * 3D machine room and is captured into the same back buffer Pixel Streaming encodes, so a light
 * surface would blow out the frame it sits on.
 *
 * The style guide authors tokens in `oklch()` and mirrors them to hex; hex is what is reproduced
 * here because Slate's FLinearColor has no oklch constructor. FromSRGB is used rather than the
 * FLinearColor(hex) path so the numbers below can be compared literally against the guide.
 *
 * TYPEFACE. The guide mandates Montserrat, and this uses it -- three faces vendored from
 * CI-Capture/assets/fonts under the SIL Open Font License and imported to /Game/Fonts. It briefly
 * shipped with Roboto substituted, on the mistaken belief that no Montserrat existed anywhere in
 * the org; it does, on several surfaces. Only three weights are carried rather than the guide's
 * five, so the ladder is approximated -- see the note above the font accessors.
 */
namespace CIBrand
{
	// --- Dark-mode palette (UI-STYLE-GUIDE.md 3.2) -------------------------------------------
	inline FLinearColor Hex(uint8 R, uint8 G, uint8 B, float A = 1.0f)
	{
		return FLinearColor(FColor(R, G, B, static_cast<uint8>(FMath::Clamp(A, 0.f, 1.f) * 255.f)));
	}

	inline FLinearColor Background()        { return Hex(0x04, 0x16, 0x20); }  // #041620 deep teal-navy
	inline FLinearColor Foreground()        { return Hex(0x88, 0xa2, 0x9e); }  // #88a29e body text
	inline FLinearColor Card()              { return Hex(0x0a, 0x22, 0x2e); }  // #0a222e card surface
	inline FLinearColor Primary()           { return Hex(0xab, 0xd4, 0xd8); }  // #abd4d8 light teal
	inline FLinearColor SecondaryFg()       { return Hex(0xf5, 0xf8, 0xfa); }  // #f5f8fa text on secondary
	inline FLinearColor MutedFg()           { return Hex(0x8f, 0x98, 0xa3); }  // #8f98a3 muted text
	inline FLinearColor Accent()            { return Hex(0x2c, 0x67, 0x6d); }  // #2c676d accent / border
	inline FLinearColor Border()            { return Hex(0x2c, 0x67, 0x6d); }  // #2c676d
	inline FLinearColor Ring()              { return Hex(0x58, 0xeb, 0xbf); }  // #58ebbf mint focus ring
	inline FLinearColor Destructive()       { return Hex(0xef, 0x70, 0x70); }  // #ef7070

	// Translucent variants. The HUD floats over rendered geometry, so card surfaces are given an
	// alpha rather than the opaque token value: an opaque #0a222e panel would hide the very scene
	// the transcript is describing. Alpha is applied to the CANONICAL colour, not a hand-picked
	// darker one, so the hue stays on-brand.
	inline FLinearColor CardOverlay(float A = 0.82f)   { FLinearColor C = Card();       C.A = A; return C; }
	inline FLinearColor ShellOverlay(float A = 0.72f)  { FLinearColor C = Background(); C.A = A; return C; }

	// --- Radius (UI-STYLE-GUIDE.md 5): base 10px, md 8px, sm 6px -----------------------------
	inline constexpr float RadiusLg() { return 10.f; }
	inline constexpr float RadiusMd() { return 8.f; }
	inline constexpr float RadiusSm() { return 6.f; }

	// --- Brushes ------------------------------------------------------------------------------
	// Function-local statics: a Slate brush handed to a widget must outlive it, and these are
	// process-lifetime constants.

	/** Card surface for one transcript entry. */
	inline const FSlateBrush* EntryCardBrush()
	{
		static const FSlateRoundedBoxBrush Brush(CardOverlay(), RadiusMd());
		return &Brush;
	}

	/** Card surface for the companion's own lines -- same surface, teal hairline outline. */
	inline const FSlateBrush* CompanionCardBrush()
	{
		static const FSlateRoundedBoxBrush Brush(CardOverlay(0.86f), RadiusMd(),
			FSlateColor(Accent()), 1.f);
		return &Brush;
	}

	/** The transcript's own backing panel. */
	inline const FSlateBrush* ShellBrush()
	{
		static const FSlateRoundedBoxBrush Brush(ShellOverlay(), RadiusLg());
		return &Brush;
	}

	/** Input row backing, so the text box reads as a control rather than floating glyphs. */
	inline const FSlateBrush* InputShellBrush()
	{
		static const FSlateRoundedBoxBrush Brush(ShellOverlay(0.85f), RadiusLg(),
			FSlateColor(Border()), 1.f);
		return &Brush;
	}

	/** Status pill. */
	inline const FSlateBrush* PillBrush()
	{
		static const FSlateRoundedBoxBrush Brush(CardOverlay(0.9f), RadiusSm(),
			FSlateColor(Border()), 1.f);
		return &Brush;
	}

	// --- Type ---------------------------------------------------------------------------------
	// REAL MONTSERRAT, as the guide mandates.
	//
	// The faces are imported as UFontFace assets under /Game/Fonts (see
	// Deployment/import_brand_font.py; the TTFs are vendored from CI-Capture/assets/fonts under the
	// SIL Open Font License, so embedding and redistribution are permitted). The org already ships
	// this family on its web surfaces, which pull it from Google Fonts -- something Slate cannot do,
	// which is why it has to be an asset here.
	//
	// Composed in C++ rather than as a UFont asset on purpose. Creating the composite UFont needs a
	// live Slate application, so it cannot be built by the -run=pythonscript commandlet the rest of
	// this project's content is authored with (it asserts CurrentApplication.IsValid() and takes the
	// editor down). Building the FCompositeFont here needs only the three face assets, which DO
	// import headlessly, so the whole font path stays reproducible from the command line.
	//
	// Weight ladder, mapping the guide's Montserrat weights onto the three vendored faces:
	//     200 -> Light      400/500 -> Regular      600/700 -> SemiBold
	//
	// FALLS BACK to the engine's Roboto if the assets are absent. A content-light checkout should
	// get a HUD in the wrong typeface, not a HUD with no text in it.
	namespace Detail
	{
		inline const UFontFace* LoadFace(const TCHAR* Path)
		{
			// Lazy and cached: this is called every frame the HUD paints, and LoadObject at static
			// init time would run before the asset registry is up.
			static TMap<FString, const UFontFace*> Cache;
			if (const UFontFace** Found = Cache.Find(Path))
			{
				return *Found;
			}
			const UFontFace* Face = LoadObject<UFontFace>(nullptr, Path);
			Cache.Add(Path, Face);
			return Face;
		}

		inline TSharedPtr<const FCompositeFont> Montserrat()
		{
			static TSharedPtr<const FCompositeFont> Composite;
			static bool bTried = false;
			if (bTried)
			{
				return Composite;
			}
			bTried = true;

			const UFontFace* Light    = LoadFace(TEXT("/Game/Fonts/Montserrat-Light.Montserrat-Light"));
			const UFontFace* Regular  = LoadFace(TEXT("/Game/Fonts/Montserrat-Regular.Montserrat-Regular"));
			const UFontFace* SemiBold = LoadFace(TEXT("/Game/Fonts/Montserrat-SemiBold.Montserrat-SemiBold"));
			if (!Regular)
			{
				// Regular is the floor: without it there is no body copy, so fall back wholesale
				// rather than render half the HUD in one family and half in another.
				return nullptr;
			}

			TSharedRef<FCompositeFont> Built = MakeShared<FCompositeFont>();
			auto Append = [&Built](const TCHAR* Name, const UFontFace* Face)
			{
				if (Face)
				{
					// FTypeface::AppendFont's public overload takes a FILENAME, which is the path
					// that breaks in a cooked container. Emplacing an FFontData built from the face
					// ASSET keeps the reference inside the package instead.
					// Named local, not FTypefaceEntry Entry(FName(Name)) -- that form is a function
					// declaration to the compiler, not a variable.
					const FName EntryName(Name);
					FTypefaceEntry Entry(EntryName);
					Entry.Font = FFontData(Face);
					Built->DefaultTypeface.Fonts.Add(MoveTemp(Entry));
				}
			};
			Append(TEXT("Light"), Light);
			Append(TEXT("Regular"), Regular);
			Append(TEXT("SemiBold"), SemiBold);
			Composite = Built;
			return Composite;
		}

		inline FSlateFontInfo Font(const TCHAR* Typeface, float Size, const char* RobotoFallback)
		{
			if (TSharedPtr<const FCompositeFont> M = Montserrat())
			{
				return FSlateFontInfo(M, Size, FName(Typeface));
			}
			return FCoreStyle::GetDefaultFontStyle(RobotoFallback, Size);
		}
	}

	inline FSlateFontInfo BodyFont()    { return Detail::Font(TEXT("Regular"), 12, "Regular"); }
	inline FSlateFontInfo SpeakerFont() { return Detail::Font(TEXT("SemiBold"), 10, "Medium"); }
	inline FSlateFontInfo StatusFont()  { return Detail::Font(TEXT("SemiBold"), 9, "Medium"); }
	inline FSlateFontInfo InputFont()   { return Detail::Font(TEXT("Regular"), 12, "Regular"); }
}

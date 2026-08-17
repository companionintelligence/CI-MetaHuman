#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
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
 * KNOWN DEVIATION -- TYPEFACE. The guide mandates Montserrat (weights 200/400/500/600/700). No
 * Montserrat face exists in this project or in CI-Common/assets: the web surfaces pull it from
 * Google Fonts, which Slate cannot do. Importing it needs a .ttf brought in as a UFont asset,
 * which is a content change, not a code one. Until then this uses the engine's default sans and
 * carries the brand through COLOR, RADIUS and SPACING, which are reproduced exactly. Do not
 * describe the HUD as fully brand-compliant while this note stands.
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
	// SUBSTITUTE FACE: Roboto, the family the engine ships in Engine/Content/Slate/Fonts.
	//
	// The guide mandates Montserrat. It is not installed on this machine and is not in
	// CI-Common/assets -- the web surfaces pull it from Google Fonts, which Slate cannot do -- and
	// no geometric alternative (Poppins, Century Gothic, Futura, Quicksand) is present either, so
	// the honest choice is the closest thing already available rather than a font that has to be
	// imported before the HUD will build. Roboto is neo-grotesque where Montserrat is geometric,
	// so the letterforms differ most on 'a', 'e' and 'g'; at the 9-12px this HUD renders, over a
	// moving 3D background, that difference is far less visible than the WEIGHT and COLOUR
	// relationships, which are reproduced exactly.
	//
	// Weight ladder maps the guide's Montserrat weights onto the Roboto faces the engine has:
	//     Montserrat 200 -> Light      400 -> Regular      500 -> Medium      600/700 -> Bold
	// Swapping in real Montserrat later is a one-line change per accessor once a UFont asset
	// exists; nothing else in the HUD refers to a face by name.
	inline FSlateFontInfo BodyFont()    { return FCoreStyle::GetDefaultFontStyle("Regular", 12); }
	inline FSlateFontInfo SpeakerFont() { return FCoreStyle::GetDefaultFontStyle("Medium", 10); }
	inline FSlateFontInfo StatusFont()  { return FCoreStyle::GetDefaultFontStyle("Medium", 9); }
	inline FSlateFontInfo InputFont()   { return FCoreStyle::GetDefaultFontStyle("Regular", 12); }
}

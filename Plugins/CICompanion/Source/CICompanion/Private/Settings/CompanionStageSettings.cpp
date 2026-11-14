#include "Settings/CompanionStageSettings.h"

UCompanionStageSettings::UCompanionStageSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("CI Companion Stage");
}

const FCompanionStageShot* UCompanionStageSettings::FindShot(FName Name) const
{
	return Shots.FindByPredicate([Name](const FCompanionStageShot& Shot) { return Shot.Name == Name; });
}

const FCompanionStageShot* UCompanionStageSettings::FindShotFor(FName Name, bool bPortrait) const
{
	if (bPortrait)
	{
		if (const FCompanionStageShot* Portrait = FindShot(FName(*(Name.ToString() + TEXT("_PORTRAIT")))))
		{
			return Portrait;
		}
	}
	return FindShot(Name);
}

bool UCompanionStageSettings::HasOpening() const
{
	const FCompanionStageShot* Opening = FindShot(OpeningShot);
	const FCompanionStageShot* Home = FindShot(HomeShot);
	return Opening && Opening->Keys.Num() >= 2 && Home && Home->Keys.Num() >= 1;
}

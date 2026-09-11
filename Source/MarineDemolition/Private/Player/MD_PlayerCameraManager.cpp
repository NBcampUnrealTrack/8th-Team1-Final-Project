// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/MD_PlayerCameraManager.h"

#include "Character/MD_Character.h"
#include "Component/Scrape/MD_ScrapeToolComponent.h"
#include "GameFramework/PlayerController.h"

void AMD_PlayerCameraManager::ApplyCameraModifiers(
	const float DeltaTime,
	FMinimalViewInfo& InOutPOV)
{
	APlayerController* const PlayerController = GetOwningPlayerController();
	AMD_Character* const Character = PlayerController
		? Cast<AMD_Character>(PlayerController->GetPawn())
		: nullptr;
	UMD_ScrapeToolComponent* const ScrapeTool = Character
		? Character->GetScrapeToolComponent()
		: nullptr;
	if (!ScrapeTool || !ScrapeTool->IsScrapeFocusViewActive())
	{
		Super::ApplyCameraModifiers(DeltaTime, InOutPOV);
		return;
	}

	const FMinimalViewInfo FocusBasePOV = InOutPOV;
	FMinimalViewInfo DiscardedModifiedPOV = InOutPOV;
	Super::ApplyCameraModifiers(DeltaTime, DiscardedModifiedPOV);

	// Shake/modifier의 시간은 소비하지만 시각적 변위는 Focus POV에 반영하지 않는다.
	InOutPOV = FocusBasePOV;
	ScrapeTool->ApplyScrapeFocusCameraView(InOutPOV);
}

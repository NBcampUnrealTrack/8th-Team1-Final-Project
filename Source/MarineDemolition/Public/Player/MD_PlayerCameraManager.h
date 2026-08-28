// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "MD_PlayerCameraManager.generated.h"

/**
 * Focus 중 최종 POV에 camera shake/modifier가 다시 회전·FOV·위치를 흔들지 못하게 한다.
 * Modifier는 임시 POV에 실행해 시간은 정상 진행시키되, Focus 표시 POV에는 변위를 반영하지 않는다.
 */
UCLASS()
class MARINEDEMOLITION_API AMD_PlayerCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	virtual void ApplyCameraModifiers(
		float DeltaTime,
		FMinimalViewInfo& InOutPOV) override;
};

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "MD_ToolDefinition.generated.h"

class UGameplayAbility;
class UTexture2D;

UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_ToolDefinition : public UDataAsset
{
	GENERATED_BODY()
	

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool")
	FText ToolName;

	// 프로토 단계에서는 임시 큐브 사용
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool")
	TObjectPtr<UStaticMesh> ToolMesh;

	// 이 도구가 부착될 소켓 이름. 비우면 캐릭터 기본 소켓(hand_r) 사용
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool")
	FName AttachSocketName;

	// 이 도구를 든 상태에서 좌클릭 시 발동될 어빌리티
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool", meta = (Categories = "Ability"))
	FGameplayTag ToolAbilityTag;

	// 임시 UI용 아이콘 (없으면 이름만 표시)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool")
	TObjectPtr<UTexture2D> ToolIcon;
};

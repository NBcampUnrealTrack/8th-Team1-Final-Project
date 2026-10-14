// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "GameFramework/Actor.h"
#include "Types/Travel/MD_TravelTypes.h"
#include "MD_TravelConsole.generated.h"

class UMD_InteractionPromptComponent;

UCLASS()
class MARINEDEMOLITION_API AMD_TravelConsole : public AActor, public IMD_Interactable
{
	GENERATED_BODY()

public:
	AMD_TravelConsole();

	virtual bool RequiresServerInteraction_Implementation() const override { return false; }
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;

	UFUNCTION(BlueprintPure, Category = "MD|Travel")
	const TSoftObjectPtr<UWorld>& GetTargetLevel() const {return TargetLevel; }

	// 위젯이 현장 버튼 라벨과 잠금 상태를 읽어가는 목록
	UFUNCTION(BlueprintPure, Category = "MD|Travel")
	const TArray<FMD_TravelSiteEntry>& GetSites() const { return Sites; }

	// 서버가 클라에서 받은 인덱스로 레벨을 되찾는다. 잠긴 현장이거나 범위 밖이면 빈 경로
	UFUNCTION(BlueprintCallable, Category = "MD|Travel")
	TSoftObjectPtr<UWorld> ResolveSiteLevel(int32 SiteIndex) const;

	// 액터별 행동 유형을 공통 프롬프트 설정에 연결하는 구성
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Interaction")
	TObjectPtr<UMD_InteractionPromptComponent> InteractionPrompt;

protected:
	virtual void BeginPlay() override;
	UPROPERTY(VisibleAnywhere, Category="Console")
	TObjectPtr<UStaticMeshComponent> Mesh;
	
	// 출발 시 이동할 레벨. Sites가 비어 있는 구버전 배치 액터를 위한 폴백
	UPROPERTY(EditAnywhere, Category="Console")
	TSoftObjectPtr<UWorld> TargetLevel;

	// 선택 가능한 현장 로스터. 기본값은 생성자에서 채운다
	UPROPERTY(EditAnywhere, Category="Console")
	TArray<FMD_TravelSiteEntry> Sites;

	// UI 관리자에 등록된 항해 화면과 레이어를 선택하는 태그
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Console", meta = (Categories="UI"))
	FGameplayTag ConsoleWidgetTag;
};

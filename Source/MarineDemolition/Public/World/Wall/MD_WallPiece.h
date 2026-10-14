// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "Gameplay/Trash/MD_TrashSortable.h"
#include "MD_WallPiece.generated.h"

class UMD_InteractionPromptComponent;

/**
 * 격벽 조각. 절단 시 드랍되며 안 타는 쓰레기로 분류한다.
 * 매일 ~10개가 나와 산재 쓰레기와 맞먹는 분리 대상이다.
 */
UCLASS()
class MARINEDEMOLITION_API AMD_WallPiece: public AActor, public IGameplayTagAssetInterface, public IMD_Interactable, public IMD_TrashSortable
{
	GENERATED_BODY()

public:
	AMD_WallPiece();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;

	/** Implements IMD_TrashSortable */
	/** 쓰레기와 달리 종류 테이블을 거치지 않는다. */
	virtual FGameplayTag GetTrashCategory_Implementation() const override;

	/** 조각벽에는 품목 보너스가 없다 (기획 05 §1.1 S+06~09는 쓰레기 4종만) */
	virtual FGameplayTag GetSortScoreBonusTag_Implementation() const override { return FGameplayTag(); }
	/** ~Implements IMD_TrashSortable */

	/** Server assigns contamination from the host wall's remaining surface-slime ratio. */
	void SetContaminated(bool bInContaminated);

	UPROPERTY(ReplicatedUsing = OnRep_Contaminated, SaveGame, VisibleAnywhere, BlueprintReadOnly, Category = "MD|Wall|Piece")
	bool bContaminated = false;

	// 액터별 행동 유형을 공통 프롬프트 설정에 연결하는 구성
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Interaction")
	TObjectPtr<UMD_InteractionPromptComponent> InteractionPrompt;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "MD|Wall|Piece")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 이 액터를 집을 때 사용할 물리 운반 방식. Blueprint 기본값에서 지정한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Interaction",
		meta = (Categories = "Interaction.Pickup.Physics"))
	FGameplayTag PickupMethodTag;

	/** 보관함이 이 벽 조각을 식별할 때 사용할 내용물 분류 태그 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Resource",
		meta = (Categories = "Resource.Storage.Content"))
	FGameplayTag StorageContentTag;

private:
	UFUNCTION()
	void OnRep_Contaminated();

	/** Server and clients use the same clean/contaminated tint. */
	void ApplyContaminationVisual();
};

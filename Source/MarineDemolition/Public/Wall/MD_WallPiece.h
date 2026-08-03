// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "Interaction/MD_Interactable.h"
#include "MD_WallPiece.generated.h"

// 격벽 조각 스텁. 절단 시 드랍되며 오염 여부만 보유. 픽업/운반은 샘플 회수 카드에서.
UCLASS()
class MARINEDEMOLITION_API AMD_WallPiece
	: public AActor
	, public IGameplayTagAssetInterface
	, public IMD_Interactable
{
	GENERATED_BODY()

public:
	AMD_WallPiece();

	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;

	// 오염 여부 지정 + 색 적용. 스폰 직후 호출
	void SetContaminated(bool bInContaminated);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Piece")
	bool bContaminated = false;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Piece")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 이 액터를 집을 때 사용할 물리 운반 방식. Blueprint 기본값에서 지정한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Interaction",
		meta = (Categories = "Interaction.Pickup.Physics"))
	FGameplayTag PickupMethodTag;

	/** 보관함이 이 벽 조각을 식별할 때 사용할 내용물 분류 태그 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Resource",
		meta = (Categories = "Resource.Storage.Content"))
	FGameplayTag StorageContentTag;
};

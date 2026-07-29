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

	/** 이 액터를 집을 때 사용할 처리 방식. Interaction.Pickup 아래 태그만 선택한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Interaction",
		meta = (Categories = "Interaction.Pickup"))
	FGameplayTag PickupMethodTag;
};

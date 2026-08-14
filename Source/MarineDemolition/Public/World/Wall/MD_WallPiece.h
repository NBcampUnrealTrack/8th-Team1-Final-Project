// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "Gameplay/Trash/MD_TrashSortable.h"
#include "MD_WallPiece.generated.h"

/**
 * 격벽 조각. 절단 시 드랍되며 오염 여부로 칸이 갈린다 (기획 02 §11 매핑표).
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
	/** 쓰레기와 달리 종류 테이블을 거치지 않는다. 칸이 오염 여부 하나에서 갈린다 */
	virtual FGameplayTag GetTrashCategory_Implementation() const override;

	/** 조각벽에는 품목 보너스가 없다 (기획 05 §1.1 S+06~09는 쓰레기 4종만) */
	virtual FGameplayTag GetSortScoreBonusTag_Implementation() const override { return FGameplayTag(); }
	/** ~Implements IMD_TrashSortable */

	// 오염 여부 지정 + 색 적용. 스폰 직후 서버에서 호출
	void SetContaminated(bool bInContaminated);

	/** 어느 칸에 넣어야 하는지가 이 값으로 갈리므로 클라이언트도 알아야 한다 */
	UPROPERTY(ReplicatedUsing = OnRep_Contaminated, VisibleAnywhere, BlueprintReadOnly, Category = "MD|Wall|Piece")
	bool bContaminated = false;

protected:
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

	/** 오염 여부를 메시 색에 반영. 서버·클라이언트 양쪽에서 같은 경로를 탄다 */
	void ApplyContaminationVisual();
};

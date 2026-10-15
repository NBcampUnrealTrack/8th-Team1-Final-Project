#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "Gameplay/Trash/MD_TrashSortable.h"
#include "Types/Trash/MD_TrashTypes.h"
#include "MD_TrashItem.generated.h"

class UMD_InteractionPromptComponent;

class UStaticMeshComponent;
class UPhysicalMaterial;
class AMD_TrashSpawnVolume;

/**
 * 기지 바닥에 산재한 각종 쓰레기 (기획 02 §1)
 * 손으로 집어 임시 쓰레기통에 담는 물리 오브젝트.
 * 정답 칸·질량·메시는 종류 테이블에서 오고, 배치 시 종류와 Variant 이름을 지정한다.
 */
UCLASS()
class MARINEDEMOLITION_API AMD_TrashItem
	: public AActor
	, public IGameplayTagAssetInterface
	, public IMD_Interactable
	, public IMD_TrashSortable
{
	GENERATED_BODY()

public:
	AMD_TrashItem();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	virtual FText GetInteractionDisplayName_Implementation() const override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;

	/** Implements IMD_TrashSortable */
	/** 정답 칸은 배치 값이 아니라 종류 테이블에서 온다. 캔에 플라스틱 칸을 달아둘 수 없다 */
	virtual FGameplayTag GetTrashCategory_Implementation() const override;
	virtual FGameplayTag GetSortScoreBonusTag_Implementation() const override;
	virtual FMD_TrashSortDescriptor GetTrashSortDescriptor_Implementation() const override;
	/** ~Implements IMD_TrashSortable */

	UFUNCTION(BlueprintPure, Category = "MD|Trash")
	FName GetTrashKindId() const { return TrashKindId; }

	/** 동적 스폰 전에 종류를 지정한다. 서버에서 바꾼 값은 클라이언트에도 복제된다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Trash")
	void SetTrashKindId(FName NewTrashKindId);

	/** Server-only source of this procedurally generated item. Carried items retain the source. */
	AMD_TrashSpawnVolume* GetOriginGenerator() const;
	void SetOriginGenerator(AMD_TrashSpawnVolume* NewOriginGenerator);
	/** Server-only, immutable with respect to carrying and world movement. */
	void SetOriginFloorCell(const FIntVector& Cell);
	bool GetOriginFloorCell(FIntVector& OutCell) const;
	FName GetOriginKindId() const { return OriginKindId; }

	/** Generated items use the mesh asset's authored centimeter size, independent of Blueprint placement defaults. */
	void UseAuthoredMeshScaleForProceduralSpawn();

	UFUNCTION(BlueprintPure, Category = "MD|Trash")
	FName GetTrashVariantId() const { return TrashVariantId; }

	/** 종류와 Variant를 함께 적용한다. 이전 None 입력은 첫 Variant. 잘못된 선택이면 변경하지 않는다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Trash")
	bool SetTrashAppearance(FName NewTrashKindId, FName NewVariantId);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Trash")
	bool SetTrashVariantId(FName NewVariantId);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif

	// 액터별 행동 유형을 공통 프롬프트 설정에 연결하는 구성
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Interaction")
	TObjectPtr<UMD_InteractionPromptComponent> InteractionPrompt;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "MD|Trash|Physics")
	TObjectPtr<UPhysicalMaterial> ContactMaterial;

	/** 배치 인스턴스에서 종류를 바꿔도 에디터에 바로 반영되게 한다 */
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/**
	 * 종류 테이블의 행 이름. 에디터에서는 테이블 행 목록이 드롭다운으로 나온다.
	 * 배치 인스턴스는 레벨 값을 사용하고, 동적 스폰 인스턴스는 서버 값을 복제한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_TrashKindId,
		Category = "MD|Trash", meta = (GetOptions = "GetTrashKindIdOptions"))
	FName TrashKindId = TEXT("DrinkCan");

	/** 이름으로 저장되므로 배열 재정렬에 영향받지 않는다. 이전 None 값은 첫 Variant로 이전한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_TrashKindId,
		Category = "MD|Trash", meta = (GetOptions = "GetTrashVariantIdOptions", DisplayName = "Variant"))
	FName TrashVariantId;

	/** 그레이박스에서 종류를 구분하기 위한 칸 색 적용. 최종 아트 메시로 교체하면 끈다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash")
	bool bTintPlaceholderMeshByCategory = true;

	/**
	 * 색을 넣을 Custom Primitive Data 인덱스. Vector4라 이 인덱스부터 4칸을 쓴다.
	 * 머티리얼(M_MD_TrashTint / M_Master_Opaque)의 색 파라미터에서
	 * "Use Custom Primitive Data"를 켜고 같은 인덱스를 지정해야 색이 들어온다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash",
		meta = (EditCondition = "bTintPlaceholderMeshByCategory", ClampMin = "0", ClampMax = "28"))
	int32 TintCustomDataIndex = 0;

	/** 이 액터를 집을 때 사용할 물리 운반 방식 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Interaction",
		meta = (Categories = "Interaction.Pickup.Physics"))
	FGameplayTag PickupMethodTag;

	/** 임시 쓰레기통이 이 쓰레기를 식별할 때 사용할 내용물 분류 태그 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Resource",
		meta = (Categories = "Resource.Storage.Content"))
	FGameplayTag StorageContentTag;

private:
	UFUNCTION()
	void OnRep_TrashKindId();

	/** TrashKindId 드롭다운 항목. 디테일 패널이 리플렉션으로 호출한다 */
	UFUNCTION()
	TArray<FName> GetTrashKindIdOptions() const;

	UFUNCTION()
	TArray<FName> GetTrashVariantIdOptions() const;

	/** 종류 행의 질량·메시·색을 메시 컴포넌트에 반영 */
	void ApplyTrashKindRow();

	/** OnConstruction 재실행이 같은 메시를 다시 동기 로드하지 않게 한다 */
	FName AppliedTrashKindId = NAME_None;
	FName AppliedTrashVariantId;
	FGameplayTag AppliedCategory;
	bool bHasAppliedTrashKind = false;

	/** Runtime origin; the generator's named ledger is persisted instead of item transforms. */
	TWeakObjectPtr<AMD_TrashSpawnVolume> OriginGenerator;
	FIntVector OriginFloorCell = FIntVector::ZeroValue;
	FName OriginKindId = NAME_None;
	bool bHasOriginFloorCell = false;
};

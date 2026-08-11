#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "MD_TrashSorter.generated.h"

class UBoxComponent;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class UStaticMeshComponent;
class AMD_GameStateBase;
class UMD_MerchantSubsystem;
struct FMD_TrashSortResult;

UCLASS()
class MARINEDEMOLITION_API AMD_TrashSorter : public AActor
{
	GENERATED_BODY()

public:
	AMD_TrashSorter();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 이 분류기가 정분류로 인정하는 칸 */
	UFUNCTION(BlueprintPure, Category = "MD|Trash")
	FGameplayTag GetAcceptedCategory() const { return AcceptedCategory; }

	/** 지금까지 삼킨 물건 수. 정분류·오분류를 가리지 않는다 */
	UFUNCTION(BlueprintPure, Category = "MD|Trash")
	int32 GetSortedCount() const { return SortedCount; }

protected:
	/** 배치 인스턴스에서 칸을 바꿔도 에디터에 바로 색이 반영되게 한다 */
	virtual void OnConstruction(const FTransform& Transform) override;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 삼킬 때 연출 훅. 소리 없이 사라지면 버그와 구분이 안 된다 */
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Trash")
	void OnItemSwallowed(bool bCorrectSort, const FVector& Location);

	/** 통째 투입은 품목별 로그 대신 합산 결과 하나만 연출한다. 통 자체는 점수에 포함하지 않는다 */
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Trash")
	void OnContainerSwallowed(
		int32 CorrectCount,
		int32 MisSortCount,
		int32 DiscardedCount,
		int32 RefundCredits,
		const FVector& Location);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 이 안에 들어오면 투입으로 본다 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash")
	TObjectPtr<UBoxComponent> IntakeVolume;

	/** 담당 칸. 드롭다운에 Bonus.* 도 뜨지만 그건 칸이 아니다 — BeginPlay에서 걸러 경고한다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash",
		meta = (Categories = "Score.Sorting"))
	FGameplayTag AcceptedCategory;

	/** 그레이박스에서 칸을 구분하기 위한 색 적용. 최종 아트로 교체하면 끈다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash")
	bool bTintPlaceholderMeshByCategory = true;

	/** 색을 넣을 벡터 파라미터 이름. 기본값은 배치용 M_MD_TrashTint의 Tint (M_Master_Opaque는 Color다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash",
		meta = (EditCondition = "bTintPlaceholderMeshByCategory"))
	FName TintParameterName = TEXT("Tint");

	/** 지금까지 삼킨 물건 수. UI 표시용이라 복제한다 */
	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "MD|Trash")
	int32 SortedCount = 0;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SorterMID;

private:
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** 액터는 서버에서 파괴되므로 위치를 실어 보낸다 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastItemSwallowed(bool bCorrectSort, FVector Location);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastContainerSwallowed(
		int32 CorrectCount,
		int32 MisSortCount,
		int32 DiscardedCount,
		int32 RefundCredits,
		FVector Location);

	/** 판정 → 점수 → 삼킴. 서버 전용 */
	void JudgeAndSwallow(AActor* Item);
	void JudgeAndSwallowContainer(AActor* ContainerActor);

	bool ResolveSortResult(AActor* Item, FMD_TrashSortResult& OutResult) const;
	bool ConsumeSortable(
		AActor* Item,
		AMD_GameStateBase* MDGameState,
		const FMD_TrashSortResult& Result,
		int32& OutRefundCredits);
	bool GetServerLedgers(
		AMD_GameStateBase*& OutGameState,
		UMD_MerchantSubsystem*& OutMerchant) const;

	TSet<TWeakObjectPtr<AActor>> ActorsBeingProcessed;

	/** 담당 칸 색을 메시에 반영 */
	void ApplyCategoryTint();
};

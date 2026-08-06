#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "MD_TrashDispenser.generated.h"

class UBoxComponent;
class UPrimitiveComponent;
class UStaticMeshComponent;

/**
 * 모선 분류장의 분류기 한 대 (기획 02 §11 "모선 분리 5종")
 * 5종 = 클래스 5개가 아니라 이 액터 5대 + AcceptedCategory 지정.
 * 넣은 물건은 판정 후 삼켜 없앤다. 다시 꺼낼 수 없어 오분류는 그 자리에서 확정된다.
 */
UCLASS()
class MARINEDEMOLITION_API AMD_TrashDispenser : public AActor
{
	GENERATED_BODY()

public:
	AMD_TrashDispenser();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 이 컨테이너가 정분류로 인정하는 칸 */
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

private:
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** 액터는 서버에서 파괴되므로 위치를 실어 보낸다 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastItemSwallowed(bool bCorrectSort, FVector Location);

	/** 판정 → 점수 → 삼킴. 서버 전용 */
	void JudgeAndSwallow(AActor* Item);

	/** 담당 칸 색을 메시에 반영 */
	void ApplyCategoryTint();
};

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "MD_SampleShelf.generated.h"

class UArrowComponent;
class UBoxComponent;
class UStaticMesh;
class UStaticMeshComponent;
class AMD_QuestItem;

/**
 * 회수된 샘플 하나의 전시 정보.
 *
 * 회수 시점에 실제 샘플 액터는 파괴되므로, 선반이 보여줄 최소 정보만 복제한다.
 * 액터를 남겨두지 않는 이유는 18:00 정산(FinalizeDailyScore)과 Day 스냅샷이
 * 월드의 AMD_SampleItem을 전수 순회하기 때문이다 — 선반은 복귀 구역이 아니라서
 * 회수한 샘플이 그대로 남아 있으면 "미회수"로 감점된다.
 */
USTRUCT()
struct FMD_ShelfDisplayEntry
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UStaticMesh> Mesh = nullptr;

	UPROPERTY()
	FVector Scale = FVector::OneVector;

	UPROPERTY()
	bool bDamaged = false;
};

/**
 * 샘플 회수 선반 (기획 09 §1.5).
 *
 * 샘플을 손에 든 상태로는 상인과 상호작용할 수 없다 — `UMD_InteractionComponent`는
 * 운반 중 E를 무조건 "놓기"로 처리하고 조준 대상도 잡지 않는다. 그래서 이 선반은
 * 별도 입력을 쓰지 않고 **"놓으면 회수"** 로 동작한다: 수거 볼륨 안에서 손을 떠난
 * 샘플을 서버가 감지해 회수 처리한다.
 *
 * 회수되면 퀘스트 진행도만 올린다. 기획 09 §1.5가 "샘플 납품 자체는 점수·₡ 0,
 * 모든 보상은 퀘스트 완료에 귀속"이라고 못박고 있어 여기서 점수를 주지 않는다.
 */
UCLASS()
class MARINEDEMOLITION_API AMD_SampleShelf : public AActor
{
	GENERATED_BODY()

public:
	AMD_SampleShelf();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnCollectionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnCollectionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	/**
	 * 볼륨 안 샘플이 손을 떠났는지 주기적으로 본다.
	 *
	 * BeginOverlap 한 번으로는 부족하다 — 플레이어는 샘플을 든 채로 선반 앞에 서므로
	 * 오버랩은 아직 들려 있을 때 발생하고, 정작 놓는 순간에는 아무 이벤트도 없다.
	 */
	void PollPendingSamples();

	/** 어느 플레이어든 이 액터를 운반 중인지. UMD_InteractionComponent::GetHeldActor 기준. */
	bool IsHeldByAnyPawn(const AActor* Target) const;

	/** 서버에서 퀘스트 진행도와 전시 항목을 추가한 뒤 제출 아이템을 제거하는 함수 */
	void CollectQuestItem(AMD_QuestItem* QuestItem);

	UFUNCTION()
	void OnRep_DisplayEntries();

	/** 복제된 전시 목록에 맞춰 슬롯 메시 컴포넌트를 다시 만든다. 모든 머신에서 돈다. */
	void RebuildDisplay();

	/**
	 * N번째 전시 칸의 월드 트랜스폼.
	 *
	 * Arrow를 2개 이상 배치했으면 그것을 칸으로 그대로 쓰고(수동 배치 우선),
	 * 하나뿐이면 그 지점을 시작점 삼아 SlotsPerRow/SlotSpacing/RowSpacing으로 자동 배열한다.
	 */
	FTransform GetSlotTransform(int32 Index) const;

	/** 수동 배치된 전시 칸. Arrow가 2개 이상일 때만 채워진다. */
	TArray<FTransform> CollectManualSlots() const;

	UPROPERTY(VisibleAnywhere, Category = "MD|Sample|Shelf")
	TObjectPtr<USceneComponent> Root;

	/** 선반 자체 외형. 필요 없으면 BP에서 메시를 비워두면 된다. */
	UPROPERTY(VisibleAnywhere, Category = "MD|Sample|Shelf")
	TObjectPtr<UStaticMeshComponent> ShelfMesh;

	/** 이 볼륨 안에서 손을 떠난 샘플이 회수된다. */
	UPROPERTY(VisibleAnywhere, Category = "MD|Sample|Shelf")
	TObjectPtr<UBoxComponent> CollectionVolume;

	/** 전시 위치 확인용. 게임에서는 숨긴다. 자식으로 Arrow를 더 추가하면 슬롯이 늘어난다. */
	UPROPERTY(VisibleAnywhere, Category = "MD|Sample|Shelf")
	TObjectPtr<UArrowComponent> DefaultSlot;

	/** 볼륨 안 샘플이 손을 떠났는지 확인하는 주기(초). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Sample|Shelf",
		meta = (ClampMin = "0.05", UIMin = "0.05"))
	float PollInterval = 0.2f;

	/**
	 * 볼륨 안 샘플의 손상·파괴 판정을 막아두는 시간(초). 머무는 동안 폴링마다 갱신한다.
	 *
	 * 선반 메시는 이미 물리 바디를 통과시키지만, 그것만으로는 부족하다 — 통과한 샘플이
	 * 바닥으로 떨어지면서 착지 충격으로 깨질 수 있다. PollInterval보다 길어야 갱신 사이에
	 * 보호가 끊기지 않는다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Sample|Shelf",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ImpactSuppressDuration = 0.6f;

	/**
	 * 한 줄에 놓을 전시 칸 수. 메인 퀘스트가 Day1~3으로 3개라 기본 3칸이다 (기획 09 §1).
	 * 반복 퀘스트 납품까지 겹치면 3개를 넘길 수 있어 넘치는 만큼 다음 줄로 내려간다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Sample|Shelf",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 SlotsPerRow = 3;

	/** 같은 줄에서 칸 사이 간격(cm). DefaultSlot의 로컬 축 기준. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Sample|Shelf")
	FVector SlotSpacing = FVector(0.f, 40.f, 0.f);

	/** 줄이 바뀔 때 이동할 간격(cm). 선반이 여러 단이면 아래 칸으로 내려가도록 쓴다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Sample|Shelf")
	FVector RowSpacing = FVector(0.f, 0.f, -35.f);

	/** 회수 결과. 전시 전용이며 실제 샘플 액터는 회수 시점에 파괴된다. */
	UPROPERTY(ReplicatedUsing = OnRep_DisplayEntries)
	TArray<FMD_ShelfDisplayEntry> DisplayEntries;

private:
	/** 볼륨 안에 있지만 아직 손에 들려 있어 회수를 못 한 퀘스트 아이템 목록 */
	UPROPERTY()
	TSet<TObjectPtr<AMD_QuestItem>> PendingQuestItems;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> DisplayMeshes;

	FTimerHandle PollTimerHandle;
};

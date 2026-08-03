#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "Interaction/MD_Grabbable.h"
#include "Interaction/MD_Interactable.h"
#include "MD_TrashBin.generated.h"

class UBoxComponent;
class UPrimitiveComponent;
class USceneComponent;
class UStaticMeshComponent;

UCLASS()
class MARINEDEMOLITION_API AMD_TrashBin
	: public AActor
	, public IGameplayTagAssetInterface
	, public IMD_Grabbable
	, public IMD_Interactable
{
	GENERATED_BODY()

public:
	AMD_TrashBin();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;
	virtual UPrimitiveComponent* GetGrabComponent_Implementation() const override;
	virtual USceneComponent* GetGrabPoint_Implementation() const override;
	virtual FVector GetHoldOffset_Implementation() const override;
	virtual FMD_HoldProfile GetHoldProfile_Implementation() const override;

	/** Implements IMD_Interactable*/
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	/** ~Implements IMD_Interactable*/

	UFUNCTION(BlueprintPure, Category = "MD|Trash Bin")
	int32 GetContentCount() const { return ContentCount; }

protected:
	/** 서버에서 내부 내용물의 진입과 이탈 추적을 시작한다. */
	virtual void BeginPlay() override;
	/** 오버랩 및 내용물 파괴 이벤트 연결을 정리한다. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Bin")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/**
	 * 물리 운반 시 잡을 위치. 현재 메시의 상단 중앙을 기본값으로 사용하며,
	 * 실제 메시의 테두리 위치에 맞게 Blueprint에서 조정한다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Interaction")
	TObjectPtr<USceneComponent> GrabPoint;

	/** 캐릭터 HoldPoint의 로컬 축 기준 운반 위치 오프셋. Blueprint 기본값에서 조정한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Interaction", meta = (Units = "cm"))
	FVector HoldOffset = FVector::ZeroVector;

	/** 이 쓰레기통을 운반하는 동안 캐릭터 PhysicsHandle에 적용할 설정 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Interaction")
	FMD_HoldProfile HoldProfile;

	/** 내부로 판정할 영역. 쓰레기통 메시 크기에 맞게 Blueprint에서 조정한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Bin")
	TObjectPtr<UBoxComponent> ContentsVolume;

	/** 이 액터를 집을 때 사용할 물리 운반 방식. Blueprint 기본값에서 지정한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Interaction",
		meta = (Categories = "Interaction.Pickup.Physics"))
	FGameplayTag PickupMethodTag;

	/** 이 보관함이 수용할 내용물 분류. Blueprint 기본값에서 지정한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Trash Bin",
		meta = (Categories = "Resource.Storage.Content"))
	FGameplayTagContainer AcceptedContentTags;

	/** 서버가 감지한 현재 쓰레기통 내부 내용물 개수 */
	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "MD|Trash Bin")
	int32 ContentCount = 0;

private:
	/** 진입 이벤트 직후 실제 볼륨 오버랩을 기준으로 내용물 목록을 갱신한다. */
	UFUNCTION()
	void HandleBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	/** 이탈 이벤트 직후 남은 컴포넌트 오버랩까지 확인해 내용물 목록을 갱신한다. */
	UFUNCTION()
	void HandleEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex);

	/** 내부에서 파괴된 내용물을 추적 목록에서 제거 */
	UFUNCTION()
	void HandleContentDestroyed(AActor* DestroyedActor);

	/** 액터가 보관함에서 허용한 내용물 태그를 소유하는지 확인 */
	bool IsAcceptedContent(const AActor* Actor) const;

	/** 실제 볼륨 오버랩을 기준으로 추적 중인 내용물 목록을 새로 갱신한다. */
	void RefreshContents();

	/** 추적 목록의 개수를 복제되는 내용물 개수에 반영 */
	void SyncContentCount();

	TSet<TWeakObjectPtr<AActor>> Contents;
};

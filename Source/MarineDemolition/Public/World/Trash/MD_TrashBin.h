#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "Gameplay/Interaction/MD_Grabbable.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "MD_TrashBin.generated.h"

class UBoxComponent;
class UPrimitiveComponent;
class USceneComponent;
class UStaticMeshComponent;

UCLASS()
class MARINEDEMOLITION_API AMD_TrashBin	: public AActor, public IGameplayTagAssetInterface, public IMD_Grabbable, public IMD_Interactable
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
	/** 서버에서 내부 내용물의 진입과 이탈 추적을 시작 */
	virtual void BeginPlay() override;
	/** 오버랩 및 내용물 파괴 이벤트 연결을 정리 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Bin")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 물리 운반 시 잡을 위치 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Interaction")
	TObjectPtr<USceneComponent> GrabPoint;

	/** 캐릭터 HoldPoint의 로컬 축 기준 운반 위치 오프셋 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Interaction", meta = (Units = "cm"))
	FVector HoldOffset = FVector::ZeroVector;

	/** 이 쓰레기통을 운반하는 동안 캐릭터 PhysicsHandle에 적용할 설정 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Interaction")
	FMD_HoldProfile HoldProfile;

	/** 내부로 판정할 영역 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Bin")
	TObjectPtr<UBoxComponent> ContentsVolume;

	/** 이 액터를 집을 때 사용할 물리 운반 방식 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Interaction",	meta = (Categories = "Interaction.Pickup.Physics"))
	FGameplayTag PickupMethodTag;

	/** 이 보관함이 수용할 내용물 분류 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Trash Bin", meta = (Categories = "Resource.Storage.Content"))
	FGameplayTagContainer AcceptedContentTags;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "MD|Trash Bin")
	int32 ContentCount = 0;

private:
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,	UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex);

	UFUNCTION()
	void HandleContentDestroyed(AActor* DestroyedActor);

	bool IsAcceptedContent(const AActor* Actor) const;
	void RefreshContents();
	void SyncContentCount();

	TSet<TWeakObjectPtr<AActor>> Contents;
};

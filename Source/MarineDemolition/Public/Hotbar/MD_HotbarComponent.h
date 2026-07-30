#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MD_HotbarComponent.generated.h"

class AActor;

USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_HotbarSlot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Hotbar")
	TSubclassOf<AActor> ItemClass;

	bool IsEmpty() const { return !ItemClass; }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDOnHotbarChanged);

/**
 * 서버가 변경하고 소유 클라이언트가 결과를 받는 고정 슬롯 핫바.
 * 각 슬롯은 월드에 다시 생성할 수 있는 액터 클래스 하나를 저장한다.
 */
UCLASS(ClassGroup = (MD), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_HotbarComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_HotbarComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Hotbar")
	bool TryAddItem(TSubclassOf<AActor> ItemClass, int32& OutSlotIndex);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Hotbar")
	bool TryRemoveItemAt(int32 SlotIndex);

	UFUNCTION(BlueprintPure, Category = "MD|Hotbar")
	TSubclassOf<AActor> GetItemClassAt(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "MD|Hotbar")
	TArray<FMD_HotbarSlot> GetSlots() const { return Slots; }

	UFUNCTION(BlueprintPure, Category = "MD|Hotbar")
	int32 GetMaxSlots() const { return MaxSlots; }

	UPROPERTY(BlueprintAssignable, Category = "MD|Hotbar")
	FMDOnHotbarChanged OnHotbarChanged;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void OnRep_Slots();

	void NotifyHotbarChanged();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Hotbar",
		meta = (AllowPrivateAccess = "true", ClampMin = "1", UIMin = "1"))
	int32 MaxSlots = 5;

	UPROPERTY(ReplicatedUsing = OnRep_Slots)
	TArray<FMD_HotbarSlot> Slots;
};

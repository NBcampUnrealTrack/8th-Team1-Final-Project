#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MD_SupplyComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnSupplyChanged, int32, NewCount);

// 안전 소모품 수량 관리. 서버가 변경하고 클라이언트는 복제로 받는다
UCLASS(ClassGroup = (MD), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_SupplyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_SupplyComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "MD|Supply")
	int32 GetSupportCount() const { return SupportCount; }

	// 1개 소모 성공 시 true. 서버에서만 호출
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Supply")
	bool TryConsumeSupport();

	// 획득(상점/보관함)용. 서버에서만 호출
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Supply")
	void AddSupport(int32 Amount);

	// 잔량 변경 시 방송. HUD가 구독한다
	UPROPERTY(BlueprintAssignable, Category = "MD|Supply")
	FMD_OnSupplyChanged OnSupplyChanged;

private:
	UFUNCTION()
	void OnRep_SupportCount();

	// 시작 보유량. 수량 제한이 핵심이므로 에디터에서 튜닝
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_SupportCount, Category = "MD|Supply",
		meta = (ClampMin = "0", AllowPrivateAccess = "true"))
	int32 SupportCount = 3;
};

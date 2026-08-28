// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "GameplayEffectTypes.h"
#include "Types/State/MD_HealthTypes.h"
#include "MD_PlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnHealthStateChanged, EMD_HealthState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMD_OnShopOwnershipChanged);

class UMD_CharacterAttributeSet;

UENUM(BlueprintType)
enum class EMD_ReturnStatus : uint8
{
    Pending  UMETA(DisplayName = "Pending"),
    Returned UMETA(DisplayName = "Returned"),
    Failed   UMETA(DisplayName = "Failed")
};


UCLASS()
class MARINEDEMOLITION_API AMD_PlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AMD_PlayerState();

	virtual void CopyProperties(APlayerState* PlayerState) override;

    void SetReturnStatus(EMD_ReturnStatus NewStatus);

    UFUNCTION(BlueprintPure, Category = "Return")
    EMD_ReturnStatus GetReturnStatus() const;

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

protected:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    virtual void BeginPlay() override;

	/** 복제된 플랫폼 닉네임을 캐릭터 이름표에 반영한다. */
	virtual void OnRep_PlayerName() override;

private:
    UPROPERTY(ReplicatedUsing = OnRep_ReturnStatus)
    EMD_ReturnStatus ReturnStatus =
        EMD_ReturnStatus::Pending;

    UFUNCTION()
    void OnRep_ReturnStatus();

    void HandleReturnStatusChanged();

protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

public:
    UFUNCTION(BlueprintPure, Category = "MD|Attribute")
    UMD_CharacterAttributeSet* GetCharacterAttributeSet() const { return CharacterAttributeSet; }

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Attribute")
    TObjectPtr<UMD_CharacterAttributeSet> CharacterAttributeSet;

	// 현재 클라이언트의 동일 PlayerState 수명 동안 유지되는 음성 재생 배율
	UPROPERTY(Transient)
	float VoiceVolumeMultiplier = 1.5f;

public:
	// 현재 클라이언트에 저장된 플레이어별 음성 재생 배율 반환
	FORCEINLINE float GetVoiceVolumeMultiplier() { return VoiceVolumeMultiplier; }

	// 현재 클라이언트의 플레이어별 음성 재생 배율 저장
	void SetVoiceVolumeMultiplier(float NewVoiceVolumeMultiplier);

	//상태 변경 알림 HUD, 이동속도 변경시 사용
	UPROPERTY(BlueprintAssignable, Category = "MD|Health")
	FMD_OnHealthStateChanged OnHealthStateChanged;

	/** 현재 플레이어의 상점 소유권 변경을 로컬 상점 UI에 전달하는 델리게이트 */
	UPROPERTY(BlueprintAssignable, Category = "MD|Shop")
	FMD_OnShopOwnershipChanged OnShopOwnershipChanged;

	/** 현재 HP로부터 상태를 계산한다. 저장된 값이 아니다. */
	UFUNCTION(BlueprintPure, Category = "MD|Health")
	EMD_HealthState GetHealthState() const;

	/** 현재 플레이어가 이미 구매한 비소모 품목인지 확인하는 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Shop")
	bool HasPurchasedShopItem(FName ItemId) const;

	/** 현재 플레이어의 비소모 품목 구매 이력을 상점 해금 판정에 제공하는 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Shop")
	TArray<FName> GetPurchasedShopItemIds() const { return PurchasedShopItemIds; }

	/** 서버에서 구매가 완료된 비소모 품목을 현재 플레이어의 소유권에 기록하는 함수 */
	bool RecordPurchasedShopItem(FName ItemId);

protected:

	/** 이 값 이하면 부상. 팀 협의 30 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Health")
	float InjuredHealthThreshold = 30.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Health")
	float ReviveDelaySeconds = 1.0f;

	/* 부활 시 회복HP비율(1.0이 완전회복)*/
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Health")
	float ReviveHealthRatio = 1.0f;

private:
	/** 현재 플레이어만 확인하는 비소모 품목 구매 이력 */
	UPROPERTY(ReplicatedUsing = OnRep_PurchasedShopItemIds)
	TArray<FName> PurchasedShopItemIds;

	/** 플레이어별 소유권 복제 후 열려 있는 상점 화면을 갱신하는 함수 */
	UFUNCTION()
	void OnRep_PurchasedShopItemIds();

	/** HP가 바뀔 때마다 호출된다. SetHealth·GE·리플리케이션 어느 경로든 여기로 들어온다. */
	void HandleHealthChanged(const FOnAttributeChangeData& ChangeData);

	/** 상태 전이를 감지해 태그를 갱신하고 방송한다. */
	void RefreshHealthState();

	/** 사망 감지 시 서버에서 부활 타이머를 건다. */
	void StartReviveTimer();

	/** 타이머 만료 시 HP를 회복한다. 상태 전이는 HP 변경으로 자동 처리된다. */
	void Revive();

	/** 직전 상태. 전이 순간만 처리하려고 기억한다. 리플리케이트 불필요. */
	EMD_HealthState CachedHealthState = EMD_HealthState::Healthy;

	FTimerHandle ReviveTimerHandle;
};

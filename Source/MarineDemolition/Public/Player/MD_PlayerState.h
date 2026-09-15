// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "UObject/PrimaryAssetId.h"
#include "GameplayEffectTypes.h"
#include "Types/State/MD_HealthTypes.h"
#include "Types/Customization/MD_CharacterAppearanceTypes.h"
#include "MD_PlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnHealthStateChanged, EMD_HealthState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMD_OnShopOwnershipChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMD_OnAppearanceChanged);

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

	/** 다른 플레이어에게 보이는 외형이 변경될 때 방송한다. 캐릭터가 DMI를 갱신하는 데 사용한다. */
	UPROPERTY(BlueprintAssignable, Category = "MD|Customization")
	FMD_OnAppearanceChanged OnAppearanceChanged;

	/** 복제된 외형 데이터를 반환한다. */
	UFUNCTION(BlueprintPure, Category = "MD|Customization")
	const FMD_CharacterAppearance& GetAppearance() const { return ReplicatedAppearance; }

	/** 서버에서 외형을 설정하고 모든 클라이언트에 복제한다. */
	void SetAppearance(const FMD_CharacterAppearance& NewAppearance);

	// 현재 플레이어의 전체 AssetId 구매 이력 확인
	UFUNCTION(BlueprintPure, Category = "MD|Shop")
	bool HasPurchasedShopItem(FPrimaryAssetId ItemAssetId) const;

	// 무료 도구 재구매와 상점 해금 판정에 사용할 구매 이력 제공
	UFUNCTION(BlueprintPure, Category = "MD|Shop")
	FORCEINLINE TArray<FPrimaryAssetId> GetPurchasedShopItemIds() const { return PurchasedShopItemIds; }

	// 구매 완료 이력과 업그레이드 소스의 구매 금지를 함께 기록
	bool RecordPurchasedShopItem(FPrimaryAssetId ItemAssetId, FPrimaryAssetId SourceAssetId = FPrimaryAssetId());

	// 판매한 도구부터 기본 도구까지 검증된 단계들의 구매 이력과 구매 금지 해제
	void ResetShopToolProgress(TArray<FPrimaryAssetId>& ToolAssetIds);

	// 업그레이드 구매로 금지된 전체 식별자 여부 검사
	UFUNCTION(BlueprintPure, Category = "MD|Shop")
	bool IsShopItemPurchaseBlocked(FPrimaryAssetId ItemAssetId);

	// 소유 플레이어의 상점 구매 금지 목록 제공
	UFUNCTION(BlueprintPure, Category = "MD|Shop")
	FORCEINLINE TArray<FPrimaryAssetId> GetBlockedShopItemIds() { return BlockedShopItemIds; }

protected:

	/** 이 값 이하면 부상. 팀 협의 30 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Health")
	float InjuredHealthThreshold = 30.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Health")
	float ReviveDelaySeconds = 1.0f;

	/* 부활 시 회복HP비율(1.0이 완전회복)*/
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Health")
	float ReviveHealthRatio = 1.0f;

	// 업그레이드 소지 여부와 무관하게 유지할 플레이어별 구매 금지 목록
	UPROPERTY(ReplicatedUsing = OnRep_ShopPurchaseState)
	TArray<FPrimaryAssetId> BlockedShopItemIds;

private:
	// 모든 클라이언트가 같은 캐릭터 외형을 표시하기 위한 복제 외형 데이터
	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedAppearance)
	FMD_CharacterAppearance ReplicatedAppearance;

	UFUNCTION()
	void OnRep_ReplicatedAppearance();

	// 현재 플레이어만 확인하는 전체 품목 구매 이력
	UPROPERTY(ReplicatedUsing = OnRep_ShopPurchaseState)
	TArray<FPrimaryAssetId> PurchasedShopItemIds;

	// 구매 이력 또는 금지 목록 복제 후 상점 화면 갱신
	UFUNCTION()
	void OnRep_ShopPurchaseState();

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

// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Game/MD_PlayerStatusWidget.h"

#include "AbilitySystemComponent.h"
#include "Components/HorizontalBox.h"
#include "Components/ProgressBar.h"
#include "TimerManager.h"

#include "Gameplay/Attribute/MD_CharacterAttributeSet.h"
#include "Player/MD_PlayerState.h"

void UMD_PlayerStatusWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 위젯이 다시 Construct될 때 현재 Oxygen 비율로 모든 Child를 다시 동기화한다.
	PreviousOxygenIndex = INDEX_NONE;
	// 재구성된 위젯에 이전 경고 재생 상태가 남지 않도록 초기화
	DangerOxygenIndex = INDEX_NONE;

	TryBindToAbilitySystem();
}

void UMD_PlayerStatusWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BindRetryTimerHandle);
	}

	if (UAbilitySystemComponent* ASC = BoundASC.Get())
	{
		ASC->GetGameplayAttributeValueChangeDelegate(
				UMD_CharacterAttributeSet::GetHealthAttribute()).Remove(HealthChangedHandle);
		ASC->GetGameplayAttributeValueChangeDelegate(
				UMD_CharacterAttributeSet::GetMaxHealthAttribute()).Remove(MaxHealthChangedHandle);
		ASC->GetGameplayAttributeValueChangeDelegate(
			UMD_CharacterAttributeSet::GetOxygenAttribute()).Remove(OxygenChangedHandle);
		ASC->GetGameplayAttributeValueChangeDelegate(
			UMD_CharacterAttributeSet::GetMaxOxygenAttribute()).Remove(MaxOxygenChangedHandle);
	}

	Super::NativeDestruct();
}

void UMD_PlayerStatusWidget::TryBindToAbilitySystem()
{
	AMD_PlayerState* MDPlayerState = GetOwningPlayerState<AMD_PlayerState>();

	UAbilitySystemComponent* ASC =
			IsValid(MDPlayerState) ? MDPlayerState->GetAbilitySystemComponent() : nullptr;

	if (!IsValid(ASC) || !IsValid(MDPlayerState->GetCharacterAttributeSet()))
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
					BindRetryTimerHandle,
					this,
					&UMD_PlayerStatusWidget::TryBindToAbilitySystem,
					0.5f,
					false
			);
		}
		return;
	}

	BoundASC = ASC;

	HealthChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(
			UMD_CharacterAttributeSet::GetHealthAttribute())
			.AddUObject(this, &UMD_PlayerStatusWidget::HandleHealthChanged);

	MaxHealthChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(
			UMD_CharacterAttributeSet::GetMaxHealthAttribute())
			.AddUObject(this, &UMD_PlayerStatusWidget::HandleHealthChanged);

	OxygenChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(
		UMD_CharacterAttributeSet::GetOxygenAttribute())
		.AddUObject(this, &UMD_PlayerStatusWidget::HandleOxygenChanged);
	MaxOxygenChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(
		UMD_CharacterAttributeSet::GetMaxOxygenAttribute())
		.AddUObject(this, &UMD_PlayerStatusWidget::HandleOxygenChanged);

	// 구독 전에 복제된 값 즉시 반영
	RefreshHealth();
	RefreshOxygen(false);
}

void UMD_PlayerStatusWidget::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	RefreshHealth();
}

void UMD_PlayerStatusWidget::RefreshHealth()
{
	// 체력 어트리뷰트 조회를 위한 플레이어 상태
	AMD_PlayerState* MDPlayerState = GetOwningPlayerState<AMD_PlayerState>();

	// 현재 플레이어의 캐릭터 어트리뷰트
	UMD_CharacterAttributeSet* Attributes =
			IsValid(MDPlayerState) ? MDPlayerState->GetCharacterAttributeSet() : nullptr;

	if (!IsValid(HealthBar) || !IsValid(Attributes))
	{
		return;
	}

	// 체력 비율 계산에 사용하는 최대 체력
	float MaxHealth = Attributes->GetMaxHealth();

	HealthBar->SetPercent(MaxHealth > 0.f ? Attributes->GetHealth() / MaxHealth : 0.f);
}

void UMD_PlayerStatusWidget::HandleOxygenChanged(const FOnAttributeChangeData& Data)
{
	// 주기 GE가 최대치에서 같은 값을 전달하면 불필요한 HUD 갱신을 생략한다.
	if (FMath::IsNearlyEqual(Data.OldValue, Data.NewValue) == true)
	{
		return;
	}

	// 감소 중인 경우에만 다음 Pop 직전 경고를 허용하기 위한 방향 판정
	bool bOxygenDecreasing = Data.NewValue < Data.OldValue;

	// 실제 산소량이 변경된 경우에만 게이지 갱신
	RefreshOxygen(bOxygenDecreasing);
}

void UMD_PlayerStatusWidget::RefreshOxygen(bool bOxygenDecreasing)
{
	// 산소 어트리뷰트 조회를 위한 플레이어 상태
	AMD_PlayerState* MDPlayerState = GetOwningPlayerState<AMD_PlayerState>();

	// 현재 플레이어의 캐릭터 어트리뷰트
	UMD_CharacterAttributeSet* Attributes = IsValid(MDPlayerState) ? MDPlayerState->GetCharacterAttributeSet() : nullptr;

	// 산소 단계 컨테이너가 바인딩되지 않은 경우 갱신 중단
	if (IsValid(HB_OxygenContainer) == false)
	{
		return;
	}

	// 산소 어트리뷰트가 준비되지 않은 경우 갱신 중단
	if (IsValid(Attributes) == false)
	{
		return;
	}

	// 최대 산소값을 사용하여 현재 산소량을 유효 범위로 제한
	float MaxOxygenValue = Attributes->GetMaxOxygen();

	// 산소 단계 계산이 불가능한 최대값이면 갱신 중단
	if (MaxOxygenValue <= 0.0f)
	{
		return;
	}

	float CurrentOxygenValue = FMath::Clamp(Attributes->GetOxygen(), 0.0f, MaxOxygenValue);
	float OxygenRatio = CurrentOxygenValue / MaxOxygenValue;

	// 현재 구성된 산소 위젯 개수를 저장한다.
	int32 OxygenWidgetCount = HB_OxygenContainer->GetChildrenCount();

	// 표시할 산소 위젯이 없는 경우 갱신을 종료한다.
	if (OxygenWidgetCount <= 0)
	{
		return;
	}

	// 산소 비율을 0일 때 -1, 최대일 때 마지막 인덱스인 연속 값으로 변환
	float OxygenIndexValue = FMath::Lerp(
		static_cast<float>(INDEX_NONE), static_cast<float>(OxygenWidgetCount - 1), OxygenRatio);

	// 연속 인덱스의 올림값을 현재 표시 단계로 사용
	int32 NewOxygenIndex = FMath::CeilToInt(OxygenIndexValue);

	// 비정상적인 어트리뷰트 값이 Child Index 범위를 벗어나지 않도록 제한
	NewOxygenIndex = FMath::Clamp(NewOxygenIndex, INDEX_NONE, OxygenWidgetCount - 1);

	// 단계가 바뀌면 직전 위젯 Pop 또는 회복 위젯 Show 처리를 먼저 요청
	if (NewOxygenIndex != PreviousOxygenIndex)
	{
		DangerOxygenIndex = INDEX_NONE;
		UpdateOxygenWidget(NewOxygenIndex, false);
	}

	// 남아 있는 산소 위젯이 없으면 경고 처리 종료
	if (NewOxygenIndex == INDEX_NONE)
	{
		return;
	}

	// 감소 중이 아니면 실행 중인 경고 애니메이션 중지
	if (bOxygenDecreasing == false)
	{
		if (DangerOxygenIndex == NewOxygenIndex)
		{
			DangerOxygenIndex = INDEX_NONE;
			UpdateOxygenWidget(NewOxygenIndex, false);
		}
		return;
	}

	// 현재 인덱스에서 다음 감소 인덱스까지 남은 연속 인덱스 차이 계산
	float OxygenIndexDifference = OxygenIndexValue - static_cast<float>(NewOxygenIndex - 1);
	bool bShouldPlayDangerAnimation = OxygenIndexDifference <= OxygenDangerIndexThreshold;
	bool bDangerAnimationPlaying = DangerOxygenIndex == NewOxygenIndex;

	// 경고 상태가 바뀌지 않은 주기 갱신은 애니메이션 재요청 생략
	if (bShouldPlayDangerAnimation == bDangerAnimationPlaying)
	{
		return;
	}

	// 현재 위젯의 경고 재생 상태 갱신
	DangerOxygenIndex = bShouldPlayDangerAnimation == true ? NewOxygenIndex : INDEX_NONE;
	UpdateOxygenWidget(NewOxygenIndex, bShouldPlayDangerAnimation);

	//// RefreshHealth와 같은 방식. 0으로 나누는 것만 막고 0~1 비율을 넘긴다
	//Pb_Oxygen->SetPercent(
	//	MaxOxygenValue > 0.f ? Attributes->GetOxygen() / MaxOxygenValue : 0.f);
}

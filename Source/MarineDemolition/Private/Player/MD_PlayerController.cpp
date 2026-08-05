// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/MD_PlayerController.h"
#include "UI/Game/MD_HUDWidget.h"
#include "UI/Equipment/MD_EquipHotbarWidget.h"

#include "Engine/World.h"
#include "World/Interaction/MD_TravelConsole.h"

#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "Gameplay/Attribute/MD_CharacterAttributeSet.h"
#include "Player/MD_PlayerState.h"

void AMD_PlayerController::ServerRequestTravel_Implementation(AMD_TravelConsole* Console)
{
	if (!IsValid(Console))
	{
		return;
	}

	// 콘솔 근처에 있는 플레이어의 요청만 수행 (임의 호출 방지)
	APawn* MyPawn = GetPawn();
	if (!IsValid(MyPawn) || FVector::Dist(MyPawn->GetActorLocation(), Console->GetActorLocation()) > 500.f)
	{
		return;
	}

	const FString MapPackage = Console->GetTargetLevel().ToSoftObjectPath().GetLongPackageName();

	if (MapPackage.IsEmpty())
	{
		return;
	}

	GetWorld()->ServerTravel(MapPackage + TEXT("?listen"));
}

void AMD_PlayerController::MD_ApplyTestDamage()
{
	AMD_PlayerState* MDPlayerState = GetPlayerState<AMD_PlayerState>();
	UAbilitySystemComponent* ASC = MDPlayerState->GetAbilitySystemComponent();
	
	if (!IsValid(ASC))
	{
		return;
	}

	//GE를 일으킨 주체 확인
	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);

	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(TestDamageEffect, 1.0f, Context);
	if (!Spec.IsValid())
	{
		return;
	}

	ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);

	MD_ShowHealth();
}

void AMD_PlayerController::MD_ShowHealth()
{
	const AMD_PlayerState* MDPlayerState = GetPlayerState<AMD_PlayerState>();
	const UMD_CharacterAttributeSet* Attributes =
		IsValid(MDPlayerState) ? MDPlayerState->GetCharacterAttributeSet() : nullptr;

	if (!IsValid(Attributes))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attribute] AttributeSet을 찾을 수 없다"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[Attribute] HP = %.1f / %.1f"),
		Attributes->GetHealth(), Attributes->GetMaxHealth());
}

void AMD_PlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	if (!HUDWidgetClass)
	{
		return;
	}

	if (IsValid(HUDWidgetInstance))
	{
		return;
	}

	HUDWidgetInstance = CreateWidget<UMD_HUDWidget>(this, HUDWidgetClass);

	if (!IsValid(HUDWidgetInstance))
	{
		return;
	}

	HUDWidgetInstance->AddToViewport();

	if (!EquipHotbarWidgetClass || IsValid(EquipHotbarWidgetInstance))
	{
		return;
	}

	EquipHotbarWidgetInstance = CreateWidget<UMD_EquipHotbarWidget>(this, EquipHotbarWidgetClass);

	if (!IsValid(EquipHotbarWidgetInstance))
	{
		return;
	}

	EquipHotbarWidgetInstance->AddToViewport();
}

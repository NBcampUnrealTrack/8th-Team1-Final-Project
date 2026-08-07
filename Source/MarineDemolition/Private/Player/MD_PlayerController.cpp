// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/MD_PlayerController.h"
#include "UI/Game/MD_HUDWidget.h"
#include "UI/Equipment/MD_EquipHotbarWidget.h"

#include "Engine/World.h"
#include "World/Interaction/MD_TravelConsole.h"
#include "World/Interaction/MD_DayEndConsole.h"
#include "Game/MD_GameModeBase.h"

#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "Gameplay/Attribute/MD_CharacterAttributeSet.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Player/MD_PlayerState.h"

#include "Engine/GameInstance.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"

#include "Data/Merchant/MD_QuestDefinition.h"
#include "Data/Merchant/MD_SiteDefinition.h"

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

void AMD_PlayerController::ServerRequestEndDay_Implementation(AMD_DayEndConsole* Console)
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

	if (AMD_GameModeBase* GM = GetWorld()->GetAuthGameMode<AMD_GameModeBase>())
	{
		GM->RequestEndDay();
	}
}

void AMD_PlayerController::MD_ApplyTestDamage()
{
	ServerApplyTestDamage();
}

void AMD_PlayerController::MD_ShowHealth()
{
	const AMD_PlayerState* MDPlayerState = GetPlayerState<AMD_PlayerState>();
	const UMD_CharacterAttributeSet* Attributes = IsValid(MDPlayerState) ? MDPlayerState->GetCharacterAttributeSet() : nullptr;

	if (!IsValid(Attributes))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attribute] AttributeSet을 찾을 수 없다"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[Attribute] HP = %.1f / %.1f"),
		Attributes->GetHealth(), Attributes->GetMaxHealth());
}

void AMD_PlayerController::ServerApplyTestDamage_Implementation()
{
	AMD_PlayerState* MDPlayerState = GetPlayerState<AMD_PlayerState>();
	if (!IsValid(MDPlayerState))
	{
		return;
	}

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
}

void AMD_PlayerController::MD_ShowHealthState()
{
	const AMD_PlayerState* MDPlayerState = GetPlayerState<AMD_PlayerState>();
	if (!IsValid(MDPlayerState))
	{
		return;
	}

	const UMD_CharacterAttributeSet* Attributes = MDPlayerState->GetCharacterAttributeSet();
	if (!IsValid(Attributes))
	{
		return;
	}
	
	const UAbilitySystemComponent* ASC = MDPlayerState->GetAbilitySystemComponent();
	if (!IsValid(ASC))
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[Health][%s] HP=%.1f 상태=%d 부상태그=%d 사망태그=%d"),
		HasAuthority() ? TEXT("Server") : TEXT("Client"),
		IsValid(Attributes) ? Attributes->GetHealth() : -1.0f,
		static_cast<int32>(MDPlayerState->GetHealthState()),
		IsValid(ASC) && ASC->HasMatchingGameplayTag(MDGameplayTags::State_Injured) ? 1 : 0,
		IsValid(ASC) && ASC->HasMatchingGameplayTag(MDGameplayTags::State_Dead) ? 1 : 0);
}

void AMD_PlayerController::MD_AddCredits(int32 Amount)
{
	ServerAddCredits(Amount);
}

void AMD_PlayerController::MD_ShowCredits()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!IsValid(GameInstance))
	{
		return;
	}

	UMD_MerchantSubsystem* Merchant = GameInstance->GetSubsystem<UMD_MerchantSubsystem>();
	if (!IsValid(Merchant))
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[Merchant][%s] Credits = %d"),HasAuthority() ? TEXT("Server") : TEXT("Client"),Merchant->GetCredits());
}

void AMD_PlayerController::ServerAddCredits_Implementation(int32 Amount)
{
	if (UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem())
	{
		Merchant->AddCredits(Amount);
	}
}

void AMD_PlayerController::MD_BeginContract()
{
	ServerBeginContract();
}

void AMD_PlayerController::MD_DumpQuests()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!IsValid(GameInstance))
	{
		return;
	}

	UMD_MerchantSubsystem* Merchant = GameInstance->GetSubsystem<UMD_MerchantSubsystem>();
	if (!IsValid(Merchant))
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[Merchant] Credits=%d Day=%d"),Merchant->GetCredits(), Merchant->GetCurrentDay());
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


/* 아래부터는 콘솔 커맨드 입력으로 디버깅용 ServerRPC래핑입니다. 미션선택 UI연결되면 삭제 */

UMD_MerchantSubsystem* AMD_PlayerController::GetMerchantSubsystem() const
{
	UGameInstance* GameInstance = GetGameInstance();

	return IsValid(GameInstance) ? GameInstance->GetSubsystem<UMD_MerchantSubsystem>() : nullptr;
}

void AMD_PlayerController::ServerBeginContract_Implementation()
{
	// DebugSite는 서버·클라 양쪽 BP에 같은 값이 들어 있으므로 서버 사본을 쓴다.
	if (UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem())
	{
		Merchant->BeginContract(DebugSite, DebugAdvancePayment);
	}
}

void AMD_PlayerController::MD_BeginDay(int32 Day)
{
	ServerBeginDay(Day);
}

void AMD_PlayerController::ServerBeginDay_Implementation(int32 Day)
{
	if (UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem())
	{
		Merchant->BeginDay(Day);
	}
}

void AMD_PlayerController::MD_AcceptRepeat(int32 Index)
{
	ServerAcceptRepeat(Index);
}

void AMD_PlayerController::ServerAcceptRepeat_Implementation(int32 Index)
{
	if (UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem())
	{
		Merchant->TryAcceptRepeatQuest(Index);
	}
}

void AMD_PlayerController::MD_DeclineRepeat(int32 Index)
{
	ServerDeclineRepeat(Index);
}

void AMD_PlayerController::ServerDeclineRepeat_Implementation(int32 Index)
{
	if (UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem())
	{
		Merchant->TryDeclineRepeatQuest(Index);
	}
}

void AMD_PlayerController::MD_Deliver(const FString& GradeName, bool bIntact)
{
	const FGameplayTag GradeTag = FGameplayTag::RequestGameplayTag(FName(*FString::Printf(TEXT("Resource.Sample.Grade.%s"), *GradeName)),/*ErrorIfNotFound=*/false);

	ServerDeliver(GradeTag, bIntact);
}

void AMD_PlayerController::ServerDeliver_Implementation(FGameplayTag GradeTag, bool bIntact)
{
	if (UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem())
	{
		Merchant->NotifySampleDelivered(GradeTag, bIntact);
	}
}

void AMD_PlayerController::MD_MarkSiteCleared()
{
	ServerMarkSiteCleared();
}

void AMD_PlayerController::ServerMarkSiteCleared_Implementation()
{
	if (UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem())
	{
		Merchant->MarkSiteCleared();
	}
}

void AMD_PlayerController::MD_FinalizeContract()
{
	ServerFinalizeContract();

}

void AMD_PlayerController::ServerFinalizeContract_Implementation()
{
	if (UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem())
	{
		Merchant->FinalizeContract();
	}
}

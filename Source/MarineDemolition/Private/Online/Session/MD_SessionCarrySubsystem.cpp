// Fill out your copyright notice in the Description page of Project Settings.

#include "Online/Session/MD_SessionCarrySubsystem.h"
#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "GameFramework/PlayerState.h"
#include "Player/MD_PlayerState.h"

FString UMD_SessionCarrySubsystem::BuildPlayerKey(const APlayerState* PlayerState)
{
	// 제거된 플레이어의 키 생성 차단
	if (IsValid(PlayerState) == false)
	{
		return FString();
	}
	// 온라인 계정 식별자를 우선 사용
	FUniqueNetIdRepl UniqueNetID = PlayerState->GetUniqueId();
	FString NetworkKey = UniqueNetID.IsValid() == true ? UniqueNetID.ToString() : FString();
	// 온라인 식별자가 없는 환경은 플레이어 이름으로 대체
	return NetworkKey.IsEmpty() == false ? NetworkKey : PlayerState->GetPlayerName();
}

bool UMD_SessionCarrySubsystem::CapturePlayer(const APlayerState* PlayerState)
{
	// 서버 권위 상태만 수집
	if (IsValid(PlayerState) == false || PlayerState->HasAuthority() == false)
	{
		return false;
	}
	FString PlayerKey = BuildPlayerKey(PlayerState);
	if (PlayerKey.IsEmpty() == true)
	{
		return false;
	}
	// Pawn이 없는 플레이어는 이전 기록 유지
	AMD_Character* PlayerCharacter = Cast<AMD_Character>(PlayerState->GetPawn());
	UMD_EquipHotbarComponent* EquipmentComponent = IsValid(PlayerCharacter) == true ? PlayerCharacter->GetHotbarComponent() : nullptr;
	if (IsValid(EquipmentComponent) == false)
	{
		return false;
	}
	const AMD_PlayerState* MDPlayerState = Cast<AMD_PlayerState>(PlayerState);
	if (IsValid(MDPlayerState) == false)
	{
		return false;
	}
	// 핫바와 구매 이력을 하나의 개인 기록으로 갱신
	FMD_PlayerData& PlayerData = PlayerFields.FindOrAdd(PlayerKey);
	PlayerData = EquipmentComponent->BuildEquipmentSaveData();
	MDPlayerState->CollectShopPurchaseState(PlayerData);
	return PlayerData.IsStructurallyValid();
}

bool UMD_SessionCarrySubsystem::RestorePlayer(const APlayerState* PlayerState)
{
	// 서버 권위 상태에만 적용
	if (IsValid(PlayerState) == false || PlayerState->HasAuthority() == false)
	{
		return false;
	}
	FString PlayerKey = BuildPlayerKey(PlayerState);
	if (PlayerKey.IsEmpty() == true)
	{
		return false;
	}
	// 보관 기록이 없는 플레이어는 기본 지급 유지
	const FMD_PlayerData* PlayerData = PlayerFields.Find(PlayerKey);
	if (PlayerData == nullptr)
	{
		return false;
	}
	AMD_Character* PlayerCharacter = Cast<AMD_Character>(PlayerState->GetPawn());
	UMD_EquipHotbarComponent* EquipmentComponent = IsValid(PlayerCharacter) == true ? PlayerCharacter->GetHotbarComponent() : nullptr;
	if (IsValid(EquipmentComponent) == false)
	{
		return false;
	}
	AMD_PlayerState* MDPlayerState = PlayerCharacter->GetPlayerState<AMD_PlayerState>();
	if (IsValid(MDPlayerState) == false || MDPlayerState != PlayerState)
	{
		return false;
	}
	// 핫바 복원 실패 시 구매 이력도 변경하지 않음
	FMD_PlayerData RestoredPlayerData = *PlayerData;
	if (EquipmentComponent->RestoreEquipmentSaveData(RestoredPlayerData) == false)
	{
		return false;
	}
	return MDPlayerState->RestoreShopPurchaseState(RestoredPlayerData);
}

void UMD_SessionCarrySubsystem::Reset()
{
	PlayerFields.Reset();
}

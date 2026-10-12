#include "Gameplay/Lore/MD_LoreSubsystem.h"

#include "Data/Lore/MD_LoreDefinition.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Gameplay/Lore/MD_LoreProfileSubsystem.h"
#include "Player/MD_PlayerController.h"

bool UMD_LoreSubsystem::TryDiscoverLore(FPrimaryAssetId LoreAssetId)
{
	// 로컬 개인 기록을 소유하는 저장 원장 조회
	UMD_LoreProfileSubsystem* Profile = GetLocalProfile();
	// 실제 콘텐츠에 존재하는 조사 대상 정의 확인
	UMD_LoreDefinition* Definition = GetLoreDefinition(LoreAssetId);
	// 로컬 사용자와 유효한 기록이 없는 수집 차단
	if (IsValid(Profile) == false || IsValid(Definition) == false || Definition->IsValidDefinition() == false)
	{
		// 무효 기록의 개인 저장 방지
		return false;
	}
	// 기존 API의 최초 발견 의미를 유지한 중복 요청 차단
	if (Profile->EnsureLoaded() == false || Profile->HasDiscoveredLore(LoreAssetId) == true)
	{
		// 저장 로드 이전과 기보유 기록의 중복 수집 종료
		return false;
	}
	// 개인 입력을 서버에 전달할 소유 컨트롤러 조회
	AMD_PlayerController* PlayerController = Cast<AMD_PlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
	// 서버 요청 경로가 준비되지 않은 수집 보류
	if (IsValid(PlayerController) == false)
	{
		// 검증 없이 개인 원장을 선반영하는 상황 방지
		return false;
	}
	// 서버 배치와 거리 검증 이후 접속 중인 팀원에게 공유
	PlayerController->ServerRequestDiscoverLore(LoreAssetId);
	// 저장 완료와 구분되는 습득 요청 전달 결과
	return true;
}

UMD_LoreDefinition* UMD_LoreSubsystem::GetLoreDefinition(FPrimaryAssetId LoreAssetId) const
{
	// 카탈로그와 월드 액터가 공유하는 정의 조회 경로
	return UMD_LoreDefinition::LoadDefinition(LoreAssetId);
}

EMD_LoreEntryState UMD_LoreSubsystem::GetLoreState(const UMD_LoreCatalog* LoreCatalog, FPrimaryAssetId LoreAssetId) const
{
	// 카탈로그 부재 시 공개할 본문이 없는 상태 반환
	if (IsValid(LoreCatalog) == false)
	{
		// 미발견 기본 상태 유지
		return EMD_LoreEntryState::Undiscovered;
	}
	// 개인 선행 기록만으로 본문 해독 상태 계산
	return LoreCatalog->ResolveLoreState(LoreAssetId, GetDiscoveredLoreAssetIds());
}

int32 UMD_LoreSubsystem::GetDiscoveredLoreCount() const
{
	// 현재 사용자의 개인 수집 개수 반환
	return GetDiscoveredLoreAssetIds().Num();
}

bool UMD_LoreSubsystem::HasDiscoveredLore(FPrimaryAssetId LoreAssetId) const
{
	// 개인 원장을 재사용하여 보유 여부 조회용 배열 복사 방지
	UMD_LoreProfileSubsystem* Profile = GetLocalProfile();
	// 개인 저장 복원 이후 단일 기록의 보유 여부 확인
	return IsValid(Profile) == true && Profile->EnsureLoaded() == true && Profile->HasDiscoveredLore(LoreAssetId) == true;
}

bool UMD_LoreSubsystem::IsCatalogComplete(const UMD_LoreCatalog* LoreCatalog) const
{
	// 비어 있는 카탈로그의 수집 완료 오판 방지
	if (IsValid(LoreCatalog) == false || LoreCatalog->LoreEntries.IsEmpty() == true)
	{
		// 유효한 수집 대상 부재 반환
		return false;
	}
	// 반복 검사에서 동일 개인 기록 재사용
	TArray<FPrimaryAssetId> PersonalIds = GetDiscoveredLoreAssetIds();
	// 카탈로그의 모든 기록을 본인이 수집했는지 확인
	for (FPrimaryAssetId LoreAssetId : LoreCatalog->LoreEntries)
	{
		// 유효하지 않거나 미보유한 기록이 있으면 미완료 처리
		if (LoreAssetId.IsValid() == false || IsValid(GetLoreDefinition(LoreAssetId)) == false || PersonalIds.Contains(LoreAssetId) == false)
		{
			// 본인에게 남아 있는 수집 대상 표시
			return false;
		}
	}
	// 본인의 전체 기록 수집 완료 반환
	return true;
}

TArray<FPrimaryAssetId> UMD_LoreSubsystem::GetDiscoveredLoreAssetIds() const
{
	// 현재 사용자에 대응하는 개인 원장 조회
	UMD_LoreProfileSubsystem* Profile = GetLocalProfile();
	// 계정 준비 이전에는 다른 사용자의 기록을 표시하지 않는 조회
	return IsValid(Profile) == true && Profile->EnsureLoaded() == true ? Profile->GetDiscoveredLoreAssetIds() : TArray<FPrimaryAssetId>();
}

UMD_LoreProfileSubsystem* UMD_LoreSubsystem::GetLocalProfile() const
{
	// GameInstance가 유지하는 첫 로컬 사용자 조회
	ULocalPlayer* LocalPlayer = GetGameInstance()->GetFirstGamePlayer();
	// 원격 참가자나 전용 서버의 개인 파일 접근 방지
	return IsValid(LocalPlayer) == true ? LocalPlayer->GetSubsystem<UMD_LoreProfileSubsystem>() : nullptr;
}

#include "Gameplay/Lore/MD_LoreProfileSubsystem.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Gameplay/Lore/MD_LoreSaveGame.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/SecureHash.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

void UMD_LoreProfileSubsystem::Deinitialize()
{
	// 종료 전 아직 파일에 반영되지 않은 발견 기록 재시도
	SavePending();
	Super::Deinitialize();
}

bool UMD_LoreProfileSubsystem::EnsureLoaded()
{
	// 트래블 중 동일 사용자의 기록 재로드와 역행 방지
	if (bLoaded == true)
	{
		// 현재 메모리 기록 사용
		return true;
	}
	// 소유 로컬 사용자 확인
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	// 사용자 생성 이전에는 다음 초기화 시점까지 대기
	if (IsValid(LocalPlayer) == false || LocalPlayer->GetLocalPlayerIndex() < 0)
	{
		// 개인 슬롯을 구분할 수 없는 초기화 보류
		return false;
	}
	// 실제 플레이 월드에 연결되기 전 임시 계정 슬롯 선택 방지
	UWorld* World = LocalPlayer->GetWorld();
	// 뷰포트와 사용자 연결이 완료될 때까지 대기
	if (IsValid(World) == false)
	{
		// 초기화 순서로 인한 PIE 저장 혼입 차단
		return false;
	}
	// 오프라인 실행에서도 로컬 사용자별 고정 슬롯 분리
	FString ProfileKey = FString::Printf(TEXT("Local_%d"), LocalPlayer->GetLocalPlayerIndex());
	// PIE와 실제 게임의 온라인 컨텍스트 분리
	IOnlineSubsystem* OnlineSubsystem = Online::GetSubsystem(World);
	// 임시 식별자를 사용하는 NULL 서버는 고정 로컬 슬롯 사용
	if (OnlineSubsystem != nullptr && OnlineSubsystem->GetSubsystemName() != FName(TEXT("NULL")))
	{
		// Steam 등 플랫폼의 계정 식별자 조회
		IOnlineIdentityPtr Identity = OnlineSubsystem->GetIdentityInterface();
		// 로그인 전 임시 슬롯에 기록되는 상황 방지
		if (Identity.IsValid() == false || Identity->GetLoginStatus(LocalPlayer->GetControllerId()) != ELoginStatus::LoggedIn)
		{
			// 실제 계정 확정까지 로드 보류
			return false;
		}
		// 표시 이름 대신 변경되지 않는 계정 ID 사용
		TSharedPtr<const FUniqueNetId> UserId = Identity->GetUniquePlayerId(LocalPlayer->GetControllerId());
		// 계정 정보 준비 이전의 잘못된 슬롯 선택 방지
		if (UserId.IsValid() == false || UserId->IsValid() == false)
		{
			// 다음 초기화 시점에 계정 조회 재시도
			return false;
		}
		// 플랫폼이 다른 동일 문자열 ID의 충돌 방지
		ProfileKey = OnlineSubsystem->GetSubsystemName().ToString() + TEXT(":") + UserId->ToString();
	}
	// 계정 문자열의 파일명 노출과 경로 문자 혼입 방지
	SaveSlotName = TEXT("MD_Lore_") + FMD5::HashAnsiString(*ProfileKey);
	// 존재하지 않는 슬롯은 신규 개인 기록으로 시작
	if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, 0) == true)
	{
		// 해당 사용자 파일에서 독립된 로어 데이터 복원
		UMD_LoreSaveGame* Save = Cast<UMD_LoreSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0));
		// 손상 파일과 미래 버전의 원본 보존
		if (IsValid(Save) == false || Save->Version != 1)
		{
			// 빈 기록으로 기존 파일을 덮어쓰는 상황 차단
			bCanSave = false;
			// 영구 기록을 복원하지 못한 원인 출력
			UE_LOG(LogTemp, Error, TEXT("[Lore] Personal lore save cannot be loaded; original file preserved: %s"), *SaveSlotName);
		}
		else
		{
			// 콘텐츠가 아직 로드되지 않은 ID도 개인 기록에 보존
			for (FPrimaryAssetId LoreAssetId : Save->DiscoveredLoreAssetIds)
			{
				// 빈 식별자만 제외하고 중복 기록 정리
				if (LoreAssetId.IsValid() == true)
				{
					// 콘텐츠 이동 이후에도 누적 기록 유지
					DiscoveredLoreAssetIds.AddUnique(LoreAssetId);
				}
			}
		}
	}
	// 원장 구독자가 재진입해도 재로드하지 않는 완료 상태
	bLoaded = true;
	// 코덱스와 로컬 원장에 복원 완료 전달
	OnPersonalLoreChanged.Broadcast();
	// 신규 또는 복원된 개인 기록 사용 허용
	return true;
}

bool UMD_LoreProfileSubsystem::RecordLore(FPrimaryAssetId LoreAssetId)
{
	// 소유 사용자 확정 전 또는 빈 기록의 저장 차단
	if (EnsureLoaded() == false || LoreAssetId.IsValid() == false)
	{
		// 무효 발견 요청 거부
		return false;
	}
	// 이미 보유한 기록은 재작성 없이 실패 저장만 재시도
	if (DiscoveredLoreAssetIds.Contains(LoreAssetId) == true)
	{
		// 이전 파일 반영 결과 반환
		return SavePending();
	}
	// 본인이 직접 조사한 새 기록만 누적
	DiscoveredLoreAssetIds.Add(LoreAssetId);
	// 저장 실패 시에도 재시도할 발견 기록 유지
	bDirty = true;
	// 열린 개인 코덱스 즉시 갱신
	OnPersonalLoreChanged.Broadcast();
	// 게임 종료를 기다리지 않고 수집 즉시 파일 반영
	return SavePending();
}

bool UMD_LoreProfileSubsystem::SavePending()
{
	// 기록 변화가 없는 경우 디스크 재작성 생략
	if (bDirty == false)
	{
		// 기존 파일이 정상일 때만 성공 반환
		return bCanSave;
	}
	// 손상 파일과 준비되지 않은 슬롯의 덮어쓰기 방지
	if (bCanSave == false || bLoaded == false || SaveSlotName.IsEmpty() == true)
	{
		// 원본과 메모리의 미저장 기록 보존
		return false;
	}
	// 현재 사용자 기록만 포함하는 저장 객체 구성
	UMD_LoreSaveGame* Save = NewObject<UMD_LoreSaveGame>();
	// 이전 세션 기록을 포함한 전체 개인 목록 저장
	Save->DiscoveredLoreAssetIds = DiscoveredLoreAssetIds;
	// 계정당 동일 슬롯을 갱신하여 파일 누적 방지
	if (UGameplayStatics::SaveGameToSlot(Save, SaveSlotName, 0) == false)
	{
		// 메모리 기록을 유지한 저장 실패 보고
		UE_LOG(LogTemp, Warning, TEXT("[Lore] Personal lore save failed; retry pending: %s"), *SaveSlotName);
		// 다음 주기와 종료 시 저장 재시도 허용
		return false;
	}
	// 파일 반영에 성공한 변경만 미저장 상태 해제
	bDirty = false;
	// 개인 기록의 디스크 반영 완료
	return true;
}

#include "Component/Lore/MD_LoreComponent.h"

#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Gameplay/Lore/MD_LoreProfileSubsystem.h"
#include "TimerManager.h"

UMD_LoreComponent::UMD_LoreComponent()
{
	// 수집 이벤트와 저장 재시도 타이머만 사용
	PrimaryComponentTick.bCanEverTick = false;
	// 호스트의 수집 목록이 다른 참가자에게 전달되는 경로 차단
	SetIsReplicatedByDefault(false);
}

void UMD_LoreComponent::BeginPlay()
{
	Super::BeginPlay();
	// 개인 사용자가 없는 전용 서버의 파일 접근 차단
	if (GetNetMode() == NM_DedicatedServer)
	{
		// 로컬 코덱스를 생성하지 않는 서버 초기화 종료
		return;
	}
	// 로컬 사용자 생성 지연과 저장 실패에 대한 재시도 예약
	GetWorld()->GetTimerManager().SetTimer(ProfileRetryHandle, this, &ThisClass::UpdateLocalProfile, 1.0f, true);
	// 이미 준비된 개인 기록의 즉시 복원
	UpdateLocalProfile();
}

void UMD_LoreComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 종료된 컴포넌트의 재시도 콜백 정리
	if (IsValid(GetWorld()) == true)
	{
		// 월드가 먼저 해제된 종료 경로의 타이머 접근 방지
		GetWorld()->GetTimerManager().ClearTimer(ProfileRetryHandle);
	}
	// 구독한 개인 원장이 존재할 때만 정리
	if (IsValid(LocalProfile) == true)
	{
		// 맵 이동 이후 이전 컴포넌트로 알림 전달 방지
		LocalProfile->OnPersonalLoreChanged.Remove(ProfileChangedHandle);
		// 연결 종료 전 미저장 기록 반영 재시도
		LocalProfile->SavePending();
	}
	Super::EndPlay(EndPlayReason);
}

EMD_LoreEntryState UMD_LoreComponent::GetLoreState(const UMD_LoreCatalog* LoreCatalog, FPrimaryAssetId LoreAssetId) const
{
	// 카탈로그와 식별자가 준비되지 않은 상태 보호
	if (IsValid(LoreCatalog) == false || LoreAssetId.IsValid() == false)
	{
		// 공개할 기록이 없는 기본 상태 반환
		return EMD_LoreEntryState::Undiscovered;
	}
	// 다른 참가자의 기록 대신 본인 선행 조건으로 해독 판정
	return LoreCatalog->ResolveLoreState(LoreAssetId, DiscoveredLoreAssetIds);
}

bool UMD_LoreComponent::HasDiscoveredLore(FPrimaryAssetId LoreAssetId) const
{
	// 개인 원장의 유효한 기록 보유 여부 반환
	return LoreAssetId.IsValid() == true && DiscoveredLoreAssetIds.Contains(LoreAssetId) == true;
}

void UMD_LoreComponent::UpdateLocalProfile()
{
	// 기존 BP의 GameState 조회에 제공할 로컬 사용자 확인
	ULocalPlayer* LocalPlayer = GetWorld()->GetGameInstance()->GetFirstGamePlayer();
	// 로컬 사용자 생성 이전의 초기화 보류
	if (IsValid(LocalPlayer) == false)
	{
		// 다음 재시도 주기까지 대기
		return;
	}
	// 맵과 독립된 개인 저장 원장 연결
	if (IsValid(LocalProfile) == false)
	{
		// 로컬 사용자별 저장 인스턴스 조회
		LocalProfile = LocalPlayer->GetSubsystem<UMD_LoreProfileSubsystem>();
		// 사용자 서브시스템 생성 이전의 접근 방지
		if (IsValid(LocalProfile) == false)
		{
			// 다음 재시도 주기까지 대기
			return;
		}
		// 수집과 로드 결과를 현재 화면 원장으로 전달
		ProfileChangedHandle = LocalProfile->OnPersonalLoreChanged.AddUObject(this, &ThisClass::RefreshLocalProfile);
	}
	// 계정 식별과 최초 파일 복원이 완료될 때까지 대기
	if (LocalProfile->EnsureLoaded() == false)
	{
		// 다른 사용자의 임시 기록 표시 차단
		return;
	}
	// 최초 로드가 구독 이전에 완료된 경우에도 현재 목록 적용
	RefreshLocalProfile();
	// 이전 수집 시 파일 반영에 실패한 기록 재시도
	LocalProfile->SavePending();
}

void UMD_LoreComponent::RefreshLocalProfile()
{
	// 아직 연결되지 않은 개인 원장 접근 차단
	if (IsValid(LocalProfile) == false)
	{
		// 기존 화면 원장 유지
		return;
	}
	// 로컬 사용자의 영구 기록 스냅샷 조회
	TArray<FPrimaryAssetId> PersonalIds = LocalProfile->GetDiscoveredLoreAssetIds();
	// 재시도 주기마다 불필요한 화면 갱신 방지
	if (DiscoveredLoreAssetIds == PersonalIds)
	{
		// 동일한 개인 기록 유지
		return;
	}
	// 팀 전체 이력이 아닌 본인 기록만 표시
	DiscoveredLoreAssetIds = MoveTemp(PersonalIds);
	// 열린 코덱스와 상인 로어 화면 갱신
	OnLoreProgressUpdated.Broadcast();
}

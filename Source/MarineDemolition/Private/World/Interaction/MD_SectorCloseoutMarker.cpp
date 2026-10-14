// Fill out your copyright notice in the Description page of Project Settings.


#include "World/Interaction/MD_SectorCloseoutMarker.h"
#include "Component/Interaction/MD_InteractionPromptComponent.h"

#include "Character/MD_Character.h"
#include "Components/StaticMeshComponent.h"
#include "Game/MD_GameStateBase.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "Player/MD_PlayerController.h"


AMD_SectorCloseoutMarker::AMD_SectorCloseoutMarker()
{
	// 인스턴스와 파생 BP가 행동 유형을 선택하는 기본 구성 생성
	InteractionPrompt = CreateDefaultSubobject<UMD_InteractionPromptComponent>(TEXT("InteractionPrompt"));
	// 기존 상호작용 동작에 대응하는 공통 문구 유형 지정
	InteractionPrompt->PromptType = EMD_InteractionPromptType::CloseSector;

	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
}

void AMD_SectorCloseoutMarker::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMD_SectorCloseoutMarker, bClosed);
}

void AMD_SectorCloseoutMarker::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	// 마감 완료된 마커는 더 조준할 이유가 없으니 강조하지 않음
	if (IsValid(Mesh) && !bClosed)
	{
		Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
		Mesh->SetRenderCustomDepth(bHighlighted);
	}
}

bool AMD_SectorCloseoutMarker::CanInteract_Implementation(AActor*) const
{
	// 이미 마감한 구역의 재상호작용과 실행 안내 차단
	return bClosed == false;
}

void AMD_SectorCloseoutMarker::Interact_Implementation(AActor* Interactor)
{
	if (bClosed)
	{
		return;
	}

	AMD_Character* InstigatorCharacter = Cast<AMD_Character>(Interactor);
	if (!IsValid(InstigatorCharacter))
	{
		return;
	}

	// 픽업 태그가 없는 인터랙터는 로컬에서 실행되므로 서버 RPC로 넘긴다
	AMD_PlayerController* PC = Cast<AMD_PlayerController>(InstigatorCharacter->GetController());
	if (IsValid(PC) && PC->IsLocalController())
	{
		PC->ServerRequestSectorCloseout(this);
	}
}

void AMD_SectorCloseoutMarker::TryCloseout()
{
	if (!HasAuthority() || bClosed)
	{
		return;
	}

	bClosed = true;
	OnRep_Closed();

	// 마감 조건 검사(잔해 정리 등, 기획 02 §3)는 후속 카드 — 프로토는 제출 즉시 인정
	if (AMD_GameStateBase* GS = GetWorld()->GetGameState<AMD_GameStateBase>())
	{
		GS->AddScoreEvent(MDGameplayTags::Score_Daily_SectorClosed);
	}
}

void AMD_SectorCloseoutMarker::RestoreClosed(bool bWasClosed)
{
    // 서버의 복원에서만 마감 여부 변경 허용
    if (HasAuthority() == false)
    {
        // 클라이언트의 권위 상태 변경 차단
        return;
    }
    // 저장 당시의 미마감 상태도 이전 플레이 상태 위에 정확히 적용
    bClosed = bWasClosed;
    // 리슨 서버와 BP 외형에 변경된 상태 통지
    OnRep_Closed();
    // 이미 접속한 클라이언트에도 복원된 마감 여부 전송
    ForceNetUpdate();
}

void AMD_SectorCloseoutMarker::OnRep_Closed()
{
	SetInteractionHighlighted_Implementation(false);
	OnCloseoutStateChanged(bClosed);
}

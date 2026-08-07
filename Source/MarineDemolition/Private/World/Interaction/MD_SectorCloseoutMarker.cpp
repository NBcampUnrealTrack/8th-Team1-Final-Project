// Fill out your copyright notice in the Description page of Project Settings.


#include "World/Interaction/MD_SectorCloseoutMarker.h"

#include "Character/MD_Character.h"
#include "Components/StaticMeshComponent.h"
#include "Game/MD_GameStateBase.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "Player/MD_PlayerController.h"


AMD_SectorCloseoutMarker::AMD_SectorCloseoutMarker()
{
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

void AMD_SectorCloseoutMarker::RestoreClosed()
{
	// 어제 마감분 복원. 점수는 스냅샷 캐리오버에 이미 포함돼 있어 재지급하지 않는다
	bClosed = true;
	OnRep_Closed();
}

void AMD_SectorCloseoutMarker::OnRep_Closed()
{
	SetInteractionHighlighted_Implementation(false);
	OnCloseoutStateChanged(bClosed);
}

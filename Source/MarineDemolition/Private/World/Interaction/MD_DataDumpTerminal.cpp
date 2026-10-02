#include "World/Interaction/MD_DataDumpTerminal.h"

#include "Component/Interaction/MD_InteractionComponent.h"
#include "Data/Equipment/MD_QuestItemDefinition.h"
#include "GameFramework/PlayerState.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Net/UnrealNetwork.h"
#include "Player/MD_PlayerController.h"
#include "Util/MD_LogUtility.h"

#define LOCTEXT_NAMESPACE "MDDataDump"

AMD_DataDumpTerminal::AMD_DataDumpTerminal()
{
	// 홀드 중에만 서버에서 켠다
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	// 홀드 진행도가 HUD 게이지로 보이도록 기본(100)보다 낮추지 않는다. 홀드 중이 아니면 변경이 없어 비용도 없다
	SetNetUpdateFrequency(30.f);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	Mesh->SetSimulatePhysics(false);

	DisplayName = LOCTEXT("DefaultDisplayName", "단말");
	LockedMessage = LOCTEXT("Locked", "전원이 끊겨 있습니다. 전력을 먼저 복구하세요.");
	BusyMessage = LOCTEXT("Busy", "다른 대원이 덤프 중입니다.");
	InventoryFullMessage = LOCTEXT("InventoryFull", "장비 슬롯이 가득 찼습니다. 슬롯을 비운 뒤 다시 시도하세요.");
	DumpCompletedMessage = LOCTEXT("DumpCompleted", "{0} 획득");
}

void AMD_DataDumpTerminal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMD_DataDumpTerminal, bLocked);
	DOREPLIFETIME(AMD_DataDumpTerminal, bDumped);
	DOREPLIFETIME(AMD_DataDumpTerminal, HoldingPawn);
	DOREPLIFETIME(AMD_DataDumpTerminal, HoldProgress);
}

void AMD_DataDumpTerminal::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority() == true && IsValid(HoldingPawn) == true)
	{
		CancelHold();
	}

	Super::EndPlay(EndPlayReason);
}

// --- IMD_Interactable ---

void AMD_DataDumpTerminal::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	bInteractionHighlighted = bHighlighted;
	RefreshHighlight();
}

void AMD_DataDumpTerminal::SetQuestHighlighted(AActor* Source, bool bHighlighted)
{
	if (bHighlighted) QuestHighlightSources.Add(Source);
	else QuestHighlightSources.Remove(Source);
	for (auto It = QuestHighlightSources.CreateIterator(); It; ++It)
		if (!It->IsValid()) It.RemoveCurrent();
	const bool bRequested = !QuestHighlightSources.IsEmpty();
	if (bQuestHighlighted == bRequested) return;
	bQuestHighlighted = bRequested;
	RefreshHighlight();
}

void AMD_DataDumpTerminal::RefreshHighlight()
{
	if (IsValid(Mesh) == true)
	{
		Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
		Mesh->SetRenderCustomDepth(bInteractionHighlighted || bQuestHighlighted);
	}
}

FText AMD_DataDumpTerminal::GetInteractionDisplayName_Implementation() const
{
	return DisplayName;
}

FText AMD_DataDumpTerminal::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	if (bDumped == true)
	{
		return LOCTEXT("PromptDumped", "덤프 완료");
	}

	if (bLocked == true)
	{
		return LOCTEXT("PromptLocked", "전원 없음");
	}

	if (IsValid(HoldingPawn) == true)
	{
		const int32 Percent = FMath::Clamp(FMath::RoundToInt(HoldProgress * 100.f), 0, 100);
		if (HoldingPawn == Interactor)
		{
			return FText::Format(LOCTEXT("PromptHolding", "데이터 덤프 중... {0}%"), FText::AsNumber(Percent));
		}

		const APlayerState* HolderState = HoldingPawn->GetPlayerState();
		const FText HolderName = IsValid(HolderState) == true
			? FText::FromString(HolderState->GetPlayerName())
			: LOCTEXT("OtherCrew", "다른 대원");
		return FText::Format(LOCTEXT("PromptBusy", "{0} 덤프 중... {1}%"), HolderName, FText::AsNumber(Percent));
	}

	return LOCTEXT("PromptIdle", "[E] 길게 눌러 데이터 덤프");
}

bool AMD_DataDumpTerminal::CanInteract_Implementation(AActor* Interactor) const
{
	// 잠김·사용 중은 조준 프롬프트로 사유를 보여 주기 위해 허용하고 Interact에서 메시지로 거절한다
	return bDumped == false;
}

void AMD_DataDumpTerminal::Interact_Implementation(AActor* Interactor)
{
	// RequiresServerInteraction 기본값 true — 서버에서만 실행된다
	if (HasAuthority() == false)
	{
		return;
	}

	APawn* Pawn = Cast<APawn>(Interactor);
	if (IsValid(Pawn) == false || bDumped == true)
	{
		return;
	}

	if (bLocked == true)
	{
		SendHUDMessage(Pawn, LockedMessage);
		return;
	}

	if (IsValid(HoldingPawn) == true)
	{
		// 같은 사람의 재입력은 진행 중인 홀드를 그대로 둔다
		if (HoldingPawn != Pawn)
		{
			SendHUDMessage(Pawn, BusyMessage);
		}
		return;
	}

	FText FailMessage;
	if (CanGrantCartridge(Pawn, FailMessage) == false)
	{
		SendHUDMessage(Pawn, FailMessage);
		return;
	}

	BeginHold(Pawn);
}

void AMD_DataDumpTerminal::InteractReleased_Implementation(AActor* Interactor)
{
	if (HasAuthority() == false || IsValid(HoldingPawn) == false || HoldingPawn != Interactor)
	{
		return;
	}

	CancelHold();
}

// --- 홀드 ---

void AMD_DataDumpTerminal::BeginHold(APawn* Pawn)
{
	HoldingPawn = Pawn;
	HoldElapsed = 0.f;
	SetHoldProgress(0.f);
	SetActorTickEnabled(true);
	ForceNetUpdate();

	// 리슨 서버는 OnRep이 오지 않으므로 직접 알린다
	OnHoldStateChanged(true);
}

void AMD_DataDumpTerminal::CancelHold()
{
	const bool bWasHolding = IsValid(HoldingPawn);

	HoldingPawn = nullptr;
	HoldElapsed = 0.f;
	SetHoldProgress(0.f);
	SetActorTickEnabled(false);
	ForceNetUpdate();

	if (bWasHolding == true)
	{
		OnHoldStateChanged(false);
	}
}

void AMD_DataDumpTerminal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority() == false)
	{
		SetActorTickEnabled(false);
		return;
	}

	if (IsValid(HoldingPawn) == false || IsHoldStillValid() == false)
	{
		CancelHold();
		return;
	}

	HoldElapsed += DeltaSeconds;
	const float Duration = FMath::Max(HoldDuration, KINDA_SMALL_NUMBER);
	SetHoldProgress(FMath::Clamp(HoldElapsed / Duration, 0.f, 1.f));

	if (HoldElapsed >= Duration)
	{
		CompleteDump();
	}
}

bool AMD_DataDumpTerminal::IsHoldStillValid() const
{
	if (IsValid(HoldingPawn) == false)
	{
		return false;
	}

	// 사망·차단·빙의 해제는 상호작용 컴포넌트의 판정을 그대로 따른다
	const UMD_InteractionComponent* Interaction = HoldingPawn->FindComponentByClass<UMD_InteractionComponent>();
	if (IsValid(Interaction) == true && Interaction->CanOwnerInteract() == false)
	{
		return false;
	}

	// 거리·시선은 UMD_InteractionComponent::ValidateInteractionTarget의 완화 판정과 같은 방식
	const FVector ViewLocation = HoldingPawn->GetPawnViewLocation();
	const FVector ViewDirection = HoldingPawn->GetViewRotation().Vector();
	const FBox Bounds = GetComponentsBoundingBox(true);
	const FVector ToClosest = Bounds.GetClosestPointTo(ViewLocation) - ViewLocation;
	const float Distance = ToClosest.Size();

	if (Distance > MaxHoldDistance)
	{
		return false;
	}

	if (Distance > KINDA_SMALL_NUMBER)
	{
		const float Alignment = FVector::DotProduct(ViewDirection, ToClosest / Distance);
		if (Alignment < MinHoldAlignment)
		{
			return false;
		}
	}

	return true;
}

void AMD_DataDumpTerminal::CompleteDump()
{
	APawn* Pawn = HoldingPawn;

	// 홀드 중 슬롯 상태가 바뀌었을 수 있으니 지급 직전에 다시 검사한다
	FText FailMessage;
	if (CanGrantCartridge(Pawn, FailMessage) == false)
	{
		SendHUDMessage(Pawn, FailMessage);
		CancelHold();
		return;
	}

	FMD_ItemGrantContext Context;
	Context.Recipient = Pawn;
	Context.Quantity = GrantQuantity;
	if (CartridgeItem->Grant(Context) == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s %s: 카트리지 지급 실패 (Item=%s)"), *UE_CALL_LOCATIONINFO, *GetName(), *GetNameSafe(CartridgeItem));
		SendHUDMessage(Pawn, InventoryFullMessage);
		CancelHold();
		return;
	}

	bDumped = true;
	CancelHold();

	SendHUDMessage(Pawn, FText::Format(DumpCompletedMessage, CartridgeItem->ItemName));
	ForceNetUpdate();

	// 리슨 서버는 OnRep이 오지 않으므로 직접 알린다
	OnDumpStateChanged(true);
}


bool AMD_DataDumpTerminal::CanGrantCartridge(APawn* Pawn, FText& OutFailMessage) const
{
	if (IsValid(CartridgeItem) == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s %s: CartridgeItem이 비어 있음"), *UE_CALL_LOCATIONINFO, *GetName());
		OutFailMessage = FText::GetEmpty();
		return false;
	}

	FMD_ItemGrantContext Context;
	Context.Recipient = Pawn;
	Context.Quantity = GrantQuantity;
	if (CartridgeItem->CanGrant(Context) == false)
	{
		// 핫바 지급 실패 사유는 사실상 빈 슬롯 없음뿐이다
		OutFailMessage = InventoryFullMessage;
		return false;
	}

	return true;
}

void AMD_DataDumpTerminal::SetHoldProgress(float NewProgress)
{
	if (FMath::IsNearlyEqual(HoldProgress, NewProgress) == true)
	{
		return;
	}

	HoldProgress = NewProgress;
	OnDumpProgressChanged.Broadcast(HoldProgress);
}

void AMD_DataDumpTerminal::SendHUDMessage(APawn* Pawn, const FText& Message)
{
	AMD_PlayerController* PlayerController = IsValid(Pawn) == true ? Cast<AMD_PlayerController>(Pawn->GetController()) : nullptr;
	if (IsValid(PlayerController) == true && Message.IsEmpty() == false)
	{
		PlayerController->ClientShowHUDMessage(Message);
	}
}

// --- 복제 알림 ---

void AMD_DataDumpTerminal::OnRep_Locked()
{
	OnLockedStateChanged(bLocked);
}

void AMD_DataDumpTerminal::OnRep_Dumped()
{
	OnDumpStateChanged(bDumped);
}

void AMD_DataDumpTerminal::OnRep_HoldingPawn()
{
	OnHoldStateChanged(IsValid(HoldingPawn));
}

void AMD_DataDumpTerminal::OnRep_HoldProgress()
{
	OnDumpProgressChanged.Broadcast(HoldProgress);
}

void AMD_DataDumpTerminal::RestorePersistentState(bool bSavedDumped, bool bSavedLocked)
{
    // 서버만 영속 상태를 적용하도록 권위 검사
    if (HasAuthority() == false)
    {
        // 클라이언트의 사용 완료 변경 차단
        return;
    }
    // 재로드 전 플레이어의 일시적인 키 누름 상태 해제
    CancelHold();
    // 한 번 지급된 카트리지의 재발급 방지
    bDumped = bSavedDumped;
    // 수리로 변경된 전원 게이트 복원
    bLocked = bSavedLocked;
    // 호스트의 완료 표시 갱신
    OnRep_Dumped();
    // 호스트의 전원 표시 갱신
    OnRep_Locked();
    // 접속 클라이언트의 상태 동기화 예약
    ForceNetUpdate();
}

#undef LOCTEXT_NAMESPACE

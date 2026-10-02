#include "World/Tutorial/MD_TutorialWallSpawner.h"

#include "Components/ChildActorComponent.h"
#include "Data/Tutorial/MD_TutorialScriptData.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "World/Trash/MD_TrashBin.h"
#include "World/Trash/MD_TrashSorter.h"
#include "World/Wall/MD_CuttableWall.h"
#include "World/Wall/MD_WallPiece.h"
#include "World/Wall/MD_WallUtility.h"

#define LOCTEXT_NAMESPACE "MD_Tutorial"

namespace
{
	/** 조각을 주울 반경을 벽 바운즈에서 잡는다. 조각은 ±Extent 안쪽에 떨어진다 */
	constexpr float PieceSearchSlackCm = 150.0f;

	/** 넘기기 키를 보는 주기. 눌렀다 뗀 것을 놓치지 않을 만큼 촘촘해야 한다 */
	constexpr float SkipPollIntervalSeconds = 0.05f;

	/** 폰·컨트롤러가 생기기를 기다리며 이동 잠금을 다시 시도하는 주기 */
	constexpr float MovementLockRetryIntervalSeconds = 0.1f;

	/** 폴링 주기의 하한. 0 이 들어와도 타이머가 폭주하지 않게 막는다 */
	constexpr float MinPollIntervalSeconds = 0.05f;

	/** 서술 한 장의 최소 표시 시간. DisplaySeconds 가 0 이어도 한 프레임에 넘어가지 않게 한다 */
	constexpr float MinNarrativeHoldSeconds = 0.1f;

	/** Collect 와 Sort 를 함께 통과시킬 때의 최대 진행 횟수. 무한 루프 방지용 상한 */
	constexpr int32 MaxChainedStepAdvances = 4;
}

AMD_TutorialWallSpawner::AMD_TutorialWallSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	// 진행 인덱스를 모든 클라이언트가 받아야 안내가 뜬다. 액터가 하나뿐이라 비용도 없다
	bAlwaysRelevant = true;

	SkipIntroKey = EKeys::SpaceBar;
	SkipHintText = LOCTEXT("SkipHint", "스페이스바 — 넘기기");

	// 도입부 문구는 여기 두지 않는다 — DA_MD_TutorialScript 가 유일한 원본이다.
	// (액터에 두면 BP·레벨 델타로만 존재해서 핫리로드에 밀린다)

	// 아래 기본값은 기획이 지정한 문구·순서다. 벽/인프라 클래스와 트랜스폼은
	// 레벨 배치 인스턴스에서 지정한다 — C++이 콘텐츠 경로를 알 이유가 없다.
	{
		// 이동 안내 1 — 벽이 없는 슬롯이다. 완료 판정은 박스 존 도달뿐이다.
		FMD_TutorialWallSlot Slot;
		Slot.SlotName = TEXT("0A_이동");
		Slot.TargetZoneTag = TEXT("Tutorial.Zone.Walk");
		Slot.Steps = {
			{ EMD_TutorialWallPhase::ReachZone,
				LOCTEXT("S0A_T", "이동"),
				LOCTEXT("S0A_B", "WASD로 이동하세요.") },
		};
		Slots.Add(MoveTemp(Slot));
	}
	{
		// 이동 안내 2 — 잠수해서 다음 구역으로. 존을 물 아래 통로 끝에 두면 잠수가 강제된다.
		FMD_TutorialWallSlot Slot;
		Slot.SlotName = TEXT("0B_잠수 이동");
		Slot.TargetZoneTag = TEXT("Tutorial.Zone.Dive");
		Slot.Steps = {
			{ EMD_TutorialWallPhase::ReachZone,
				LOCTEXT("S0B_T", "잠수"),
				LOCTEXT("S0B_B", "잠수 후 다음 지역으로 이동하세요.") },
		};
		Slots.Add(MoveTemp(Slot));
	}
	{
		FMD_TutorialWallSlot Slot;
		Slot.SlotName = TEXT("1_정화 후 철거");
		Slot.ExpectedSortCategory = MDGameplayTags::Score_Sorting_NonBurnable;
		Slot.Steps = {
			{ EMD_TutorialWallPhase::Cut,
				LOCTEXT("S1_Cut_T", "격벽 철거"),
				LOCTEXT("S1_Cut_B", "절단기로 벽을 철거하세요.") },
			{ EMD_TutorialWallPhase::Collect,
				LOCTEXT("S1_Collect_T", "조각 수거"),
				LOCTEXT("S1_Collect_B", "쓰레기통을 꺼내 조각벽을 담고 분리수거하세요.") },
			{ EMD_TutorialWallPhase::Sort,
				LOCTEXT("S1_Sort_T", "안 타는 쓰레기"),
				LOCTEXT("S1_Sort_B",
					"깨끗한 조각벽은 안 타는 쓰레기입니다.\n"
					"분리에 성공하면 점수가 오르고, 잘못 분리하면 점수와 돈이 차감됩니다.") },
		};
		Slots.Add(MoveTemp(Slot));
	}
	{
		FMD_TutorialWallSlot Slot;
		Slot.SlotName = TEXT("3_배관");
		Slot.bCutOnlyAtUtility = true;
		Slot.bDestroyPiecesOnRepair = true;
		Slot.BlockedCutMessage = LOCTEXT("S3_Blocked",
			"이 자리는 절단기가 먹지 않습니다. 스캔으로 드러난 배관 위를 자르세요.");
		Slot.Steps = {
			{ EMD_TutorialWallPhase::Scan,
				LOCTEXT("S3_Scan_T", "격벽 스캔"),
				LOCTEXT("S3_Scan_B", "스캐너로 벽을 스캔해보세요.") },
			{ EMD_TutorialWallPhase::Cut,
				LOCTEXT("S3_Cut_T", "배관 파열"),
				LOCTEXT("S3_Cut_B", "배관을 피하지 않고 벽을 철거하면 배관이 터집니다.") },
			{ EMD_TutorialWallPhase::Repair,
				LOCTEXT("S3_Repair_T", "배관 수리"),
				LOCTEXT("S3_Repair_B", "폼건으로 터진 배관을 수리하세요.") },
		};
		Slots.Add(MoveTemp(Slot));
	}
	{
		FMD_TutorialWallSlot Slot;
		Slot.SlotName = TEXT("4_전선");
		Slot.bCutOnlyAtUtility = true;
		Slot.bDestroyPiecesOnRepair = true;
		Slot.BlockedCutMessage = LOCTEXT("S4_Blocked",
			"이 자리는 절단기가 먹지 않습니다. 스캔으로 드러난 전선 위를 자르세요.");
		Slot.Steps = {
			{ EMD_TutorialWallPhase::Scan,
				LOCTEXT("S4_Scan_T", "격벽 스캔"),
				LOCTEXT("S4_Scan_B", "스캐너로 벽을 스캔해보세요.") },
			{ EMD_TutorialWallPhase::Cut,
				LOCTEXT("S4_Cut_T", "전선 손상"),
				LOCTEXT("S4_Cut_B", "전선을 피하지 않고 벽을 철거하면 전선이 끊어집니다.") },
			{ EMD_TutorialWallPhase::Repair,
				// 배관과 달리 전선은 폼건이 아니라 절연 도구를 요구한다
				// (AMD_PowerConduit::RequiredRepairToolTag)
				LOCTEXT("S4_Repair_T", "전선 수리"),
				LOCTEXT("S4_Repair_B", "절연 도구로 끊어진 전선을 수리하세요.") },
		};
		Slots.Add(MoveTemp(Slot));
	}
	{
		// 5번: 쓰레기 스포너가 완성되면 따개비와 같이 스폰하는 슬롯이 된다.
		// 지금은 자리만 잡아 둔다 — 켜도 Steps가 비어 있어 건너뛴다.
		FMD_TutorialWallSlot Slot;
		Slot.SlotName = TEXT("5_보류_따개비");
		Slot.bEnabled = false;
		Slots.Add(MoveTemp(Slot));
	}
}

void AMD_TutorialWallSpawner::PostLoad()
{
	Super::PostLoad();
	RemoveRetiredCleaningLessons();
}

void AMD_TutorialWallSpawner::RemoveRetiredCleaningLessons()
{
	for (FMD_TutorialWallSlot& Slot : Slots)
	{
		// Preserve the original slot key used by ScriptData for its cutting/sorting text.
		Slot.Steps.RemoveAll([](const FMD_TutorialGuideStep& Step)
		{
			return Step.Phase == EMD_TutorialWallPhase::Scrape;
		});
		// Dirty-debris sorting was part of the removed wall cleaning lesson.
		if (Slot.SlotName == FName(TEXT("2_미정화 철거"))
			|| Slot.ExpectedSortCategory.MatchesTagExact(MDGameplayTags::Score_Sorting_Contaminated))
		{
			Slot.bEnabled = false;
		}
	}
}

void AMD_TutorialWallSpawner::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMD_TutorialWallSpawner, CurrentSlotIndex);
	DOREPLIFETIME(AMD_TutorialWallSpawner, CurrentStepIndex);
	DOREPLIFETIME(AMD_TutorialWallSpawner, NoticeSerial);
	DOREPLIFETIME(AMD_TutorialWallSpawner, PendingNoticeMessage);
}

void AMD_TutorialWallSpawner::BeginPlay()
{
	Super::BeginPlay();
	RemoveRetiredCleaningLessons();

	if (!HasAuthority())
	{
		return;
	}

	// 분류기는 레벨에 배치되므로 여기서 전부 잡힌다. 슬롯마다 다시 붙이지 않고
	// 핸들러에서 현재 단계로 거른다.
	for (TActorIterator<AMD_TrashSorter> It(GetWorld()); It; ++It)
	{
		AMD_TrashSorter* Sorter = *It;
		Sorter->OnTrashSorted.AddDynamic(this, &AMD_TutorialWallSpawner::HandleTrashSorted);
		BoundSorters.Add(Sorter);
	}

	// 분류기를 하나도 못 잡았으면 Sort 단계가 절대 끝나지 않는다. 미리 알 수 있게 남긴다.
	UE_LOG(LogTemp, Log, TEXT("[Tutorial] 분류기 %d개 구독"), BoundSorters.Num());
	if (BoundSorters.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Tutorial] 레벨에 분류기가 없다 — 조각벽 분리 단계를 넘어갈 수 없다"));
	}

	if (!ScriptData)
	{
		// 조용히 넘어가면 "도입부가 왜 안 뜨지" 로 시간을 버린다. 실제로 그랬다.
		UE_LOG(LogTemp, Warning,
			TEXT("MD_TutorialWallSpawner: ScriptData 가 비어 있어 도입부를 건너뛴다 "
				 "(DA_MD_TutorialScript 를 지정할 것)"));
	}

	if (GetIntroPages().Num() > 0)
	{
		EnterIntroPage(0);
		return;
	}

	BeginSlotFrom(0);
}

void AMD_TutorialWallSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetPollingEnabled(false);

	// 잠금을 걸어둔 채 사라지면 플레이어가 영구히 못 움직인다.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(IntroPageTimerHandle);
		World->GetTimerManager().ClearTimer(SkipPollTimerHandle);
		World->GetTimerManager().ClearTimer(MovementLockRetryHandle);
	}
	CurrentStepIndex = INDEX_NONE;
	ApplyMovementLock();

	ReleaseSlotActors();

	for (const TWeakObjectPtr<AMD_TrashSorter>& Sorter : BoundSorters)
	{
		if (Sorter.IsValid())
		{
			Sorter->OnTrashSorted.RemoveDynamic(this, &AMD_TutorialWallSpawner::HandleTrashSorted);
		}
	}
	BoundSorters.Reset();

	Super::EndPlay(EndPlayReason);
}

//~ 대본 조회 -------------------------------------------------------------------

const TArray<FMD_TutorialGuideStep>& AMD_TutorialWallSpawner::GetIntroPages() const
{
	if (ScriptData)
	{
		return ScriptData->IntroPages;
	}

	// 대본이 없으면 도입부를 건너뛴다. 진행이 막히는 것보다 낫다 (BeginPlay 에서 경고한다).
	static const TArray<FMD_TutorialGuideStep> Empty;
	return Empty;
}

FText AMD_TutorialWallSpawner::ResolveSkipHint() const
{
	if (ScriptData && !ScriptData->SkipHintText.IsEmpty())
	{
		return ScriptData->SkipHintText;
	}
	return SkipHintText;
}

const FMD_TutorialGuideText* AMD_TutorialWallSpawner::FindActiveGuideText() const
{
	const FMD_TutorialWallSlot* const Slot = GetActiveSlot();
	const FMD_TutorialGuideStep* const Step = GetActiveStep();
	if (!ScriptData || !Slot || !Step || IsPlayingIntro())
	{
		return nullptr;
	}
	return ScriptData->FindGuideText(Slot->SlotName, Step->Phase);
}

FText AMD_TutorialWallSpawner::ResolveSpeaker(const FMD_TutorialGuideStep& Step) const
{
	if (!Step.Speaker.IsEmpty())
	{
		return Step.Speaker;
	}
	// 대본의 기본 화자는 **서술에만** 적용한다.
	// 조작 안내에 이름표가 붙으면 "브릭스가 지금 이걸 말하고 있다"로 읽혀
	// WASD 안내 같은 시스템 문구까지 대사처럼 보인다.
	if (Step.Phase == EMD_TutorialWallPhase::Narrative && ScriptData)
	{
		return ScriptData->SpeakerName;
	}
	return FText::GetEmpty();
}

//~ 도입부 ---------------------------------------------------------------------

void AMD_TutorialWallSpawner::EnterIntroPage(const int32 PageIndex)
{
	const TArray<FMD_TutorialGuideStep>& Pages = GetIntroPages();
	if (!Pages.IsValidIndex(PageIndex))
	{
		return;
	}

	// 도입부는 CurrentSlotIndex 를 INDEX_NONE 으로 둔 채 StepIndex 만 쓴다.
	// 슬롯 배열을 건드리지 않으므로 배치된 스포너의 슬롯 데이터가 그대로 유효하다.
	CurrentSlotIndex = INDEX_NONE;
	CurrentStepIndex = PageIndex;
	ForceNetUpdate();
	BroadcastGuidance();
	ApplyMovementLock();

	const float Hold = FMath::Max(MinNarrativeHoldSeconds, Pages[PageIndex].DisplaySeconds);
	FTimerManager& Timers = GetWorldTimerManager();
	Timers.SetTimer(IntroPageTimerHandle, this, &AMD_TutorialWallSpawner::AdvanceIntro, Hold, false);

	// 넘기기 폴링은 도입부 전체에 걸쳐 한 번만 돌리면 된다.
	if (SkipIntroKey.IsValid() && !Timers.IsTimerActive(SkipPollTimerHandle))
	{
		Timers.SetTimer(
			SkipPollTimerHandle, this, &AMD_TutorialWallSpawner::PollIntroSkip,
			SkipPollIntervalSeconds, true);
	}
}

void AMD_TutorialWallSpawner::AdvanceIntro()
{
	const int32 Next = CurrentStepIndex + 1;
	if (GetIntroPages().IsValidIndex(Next))
	{
		EnterIntroPage(Next);
		return;
	}

	// 도입부 종료 — 이동 잠금은 BeginSlotFrom 이 부르는 EnterStep 에서 풀린다.
	GetWorldTimerManager().ClearTimer(IntroPageTimerHandle);
	GetWorldTimerManager().ClearTimer(SkipPollTimerHandle);
	BeginSlotFrom(0);
}

void AMD_TutorialWallSpawner::PollIntroSkip()
{
	UWorld* World = GetWorld();
	APlayerController* PC = (World && GEngine) ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
	if (!PC || !IsPlayingIntro())
	{
		bSkipKeyWasDown = false;
		return;
	}

	const bool bDown = PC->IsInputKeyDown(SkipIntroKey);
	const bool bJustPressed = bDown && !bSkipKeyWasDown;
	bSkipKeyWasDown = bDown;

	if (bJustPressed)
	{
		// 키를 떼기 전까지는 다시 걸리지 않으므로 한 번 누르면 한 장만 넘어간다.
		AdvanceIntro();
	}
}

//~ 이동 잠금 ------------------------------------------------------------------

void AMD_TutorialWallSpawner::ApplyMovementLock()
{
	const bool bWantLock = bLockMovementDuringIntro && IsPlayingIntro();
	if (bWantLock == bMovementLockApplied)
	{
		return;
	}

	// 반드시 "로컬" 컨트롤러여야 한다. GetFirstPlayerController 를 쓰면 데디케이티드 서버가
	// 남의 클라이언트 프록시에 잠금을 걸어버린다. 데디케이티드에서는 여기서 null 이 나온다.
	UWorld* World = GetWorld();
	APlayerController* PC = (World && GEngine) ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
	if (!PC)
	{
		// BeginPlay 시점엔 컨트롤러가 아직 없을 수 있다. 잠글 것이 남아 있으면 다시 시도한다.
		if (bWantLock && World && !World->GetTimerManager().IsTimerActive(MovementLockRetryHandle))
		{
			World->GetTimerManager().SetTimer(
				MovementLockRetryHandle, this, &AMD_TutorialWallSpawner::ApplyMovementLock,
				MovementLockRetryIntervalSeconds, true);
		}
		return;
	}

	PC->SetIgnoreMoveInput(bWantLock);
	if (bLockLookDuringIntro)
	{
		PC->SetIgnoreLookInput(bWantLock);
	}
	bMovementLockApplied = bWantLock;

	if (World)
	{
		World->GetTimerManager().ClearTimer(MovementLockRetryHandle);
	}
}

//~ 진행 제어 ------------------------------------------------------------------

void AMD_TutorialWallSpawner::BeginSlotFrom(const int32 SearchStart)
{
	for (int32 Index = FMath::Max(0, SearchStart); Index < Slots.Num(); ++Index)
	{
		const FMD_TutorialWallSlot& Slot = Slots[Index];
		if (!Slot.bEnabled || Slot.Steps.Num() == 0)
		{
			continue;
		}

		CurrentSlotIndex = Index;
		if (!SpawnSlotActors(Slot))
		{
			// 스폰이 실패한 슬롯에서 멈추면 튜토리얼 전체가 막힌다. 로그만 남기고 넘어간다.
			UE_LOG(LogTemp, Warning,
				TEXT("MD_TutorialWallSpawner: 슬롯 %d(%s) 스폰 실패 — 건너뛴다"),
				Index, *Slot.SlotName.ToString());
			ReleaseSlotActors();
			continue;
		}

		EnterStep(0);
		return;
	}

	FinishAll();
}

void AMD_TutorialWallSpawner::EnterStep(const int32 StepIndex)
{
	CurrentStepIndex = StepIndex;
	ForceNetUpdate();
	BroadcastGuidance();
	// 슬롯에 들어왔다는 건 도입부가 끝났다는 뜻이다 — 여기서 이동이 풀린다.
	ApplyMovementLock();

	// 어느 단계에서 멈췄는지 로그만 보고 알 수 있게 남긴다.
	// 진행이 막혔다는 제보가 오면 이 줄이 첫 단서다.
	if (const FMD_TutorialWallSlot* const LogSlot = GetActiveSlot())
	{
		const FMD_TutorialGuideStep* const LogStep = GetActiveStep();
		UE_LOG(LogTemp, Log, TEXT("[Tutorial] 슬롯 %d(%s) 단계 %d/%d 진입 — %s"),
			CurrentSlotIndex,
			*LogSlot->SlotName.ToString(),
			StepIndex + 1,
			LogSlot->Steps.Num(),
			LogStep
				? *StaticEnum<EMD_TutorialWallPhase>()->GetNameStringByValue(
					static_cast<int64>(LogStep->Phase))
				: TEXT("?"));
	}

	// 델리게이트가 없는 단계만 폴링한다
	const FMD_TutorialGuideStep* Step = GetActiveStep();
	const bool bNeedsPoll = Step
		&& (Step->Phase == EMD_TutorialWallPhase::Collect
			|| Step->Phase == EMD_TutorialWallPhase::Repair
			|| Step->Phase == EMD_TutorialWallPhase::ReachZone);

	// 존은 단계마다 한 번만 찾아 캐시한다.
	ActiveZone.Reset();
	if (Step && Step->Phase == EMD_TutorialWallPhase::ReachZone)
	{
		const FMD_TutorialWallSlot* Slot = GetActiveSlot();
		AActor* Zone = Slot ? FindZoneByTag(Slot->TargetZoneTag) : nullptr;
		if (!Zone)
		{
			// 존이 없으면 이 단계는 영원히 안 끝난다. 막히는 것보다 넘기는 게 낫다.
			UE_LOG(LogTemp, Warning,
				TEXT("MD_TutorialWallSpawner: 존 태그 '%s' 를 가진 액터가 없다 — 이 단계를 건너뛴다"),
				Slot ? *Slot->TargetZoneTag.ToString() : TEXT("(없음)"));
			SetPollingEnabled(false);
			AdvanceStep();
			return;
		}
		ActiveZone = Zone;
	}

	SetPollingEnabled(bNeedsPoll);
}

void AMD_TutorialWallSpawner::AdvanceStep()
{
	const FMD_TutorialWallSlot* Slot = GetActiveSlot();
	if (!Slot)
	{
		return;
	}

	if (Slot->Steps.IsValidIndex(CurrentStepIndex + 1))
	{
		EnterStep(CurrentStepIndex + 1);
		return;
	}

	FinishCurrentSlot();
}

void AMD_TutorialWallSpawner::FinishCurrentSlot()
{
	const int32 NextSlot = CurrentSlotIndex + 1;
	SetPollingEnabled(false);
	ReleaseSlotActors();
	BeginSlotFrom(NextSlot);
}

void AMD_TutorialWallSpawner::FinishAll()
{
	CurrentSlotIndex = Slots.Num();
	CurrentStepIndex = INDEX_NONE;
	SetPollingEnabled(false);
	ForceNetUpdate();
	BroadcastGuidance();
	ApplyMovementLock();
}

//~ 슬롯 액터 -----------------------------------------------------------------

bool AMD_TutorialWallSpawner::SlotRequiresWall(const FMD_TutorialWallSlot& Slot)
{
	for (const FMD_TutorialGuideStep& Step : Slot.Steps)
	{
		switch (Step.Phase)
		{
		case EMD_TutorialWallPhase::Cut:
		case EMD_TutorialWallPhase::Collect:
		case EMD_TutorialWallPhase::Sort:
		case EMD_TutorialWallPhase::Scan:
		case EMD_TutorialWallPhase::Repair:
			return true;
		default:
			break;
		}
	}
	return false;
}

bool AMD_TutorialWallSpawner::SpawnSlotActors(const FMD_TutorialWallSlot& Slot)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// 이동 안내처럼 벽이 필요 없는 슬롯은 스폰할 것이 없다.
	if (!SlotRequiresWall(Slot))
	{
		return true;
	}

	if (!Slot.WallClass)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("MD_TutorialWallSpawner: 슬롯 %s 는 벽이 필요한데 WallClass가 비어 있다"),
			*Slot.SlotName.ToString());
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AMD_CuttableWall* Wall = World->SpawnActor<AMD_CuttableWall>(
		Slot.WallClass,
		Slot.WallRelativeTransform * GetActorTransform(),
		SpawnParams);
	if (!Wall)
	{
		return false;
	}

	ActiveWall = Wall;
	Wall->OnWallDemolished.AddDynamic(this, &AMD_TutorialWallSpawner::HandleWallDemolished);
	Wall->OnWallScanned.AddDynamic(this, &AMD_TutorialWallSpawner::HandleWallScanned);

	// 벽 BP 가 배관·전선을 직접 품는 경우(BP_MD_CuttableWall_InPipe / _InWire)를 먼저 거둔다.
	CollectUtilitiesFromWall(Wall);

	// 슬롯에 UtilityClass 가 따로 지정된 경우에만 별도 액터로 스폰한다.
	// 인프라는 반드시 벽 다음이어야 한다 — AMD_WallUtility가 BeginPlay에서
	// LinkTrigger로 겹친 벽을 찾아 철거 델리게이트를 구독하기 때문이다.
	if (Slot.UtilityClass)
	{
		AMD_WallUtility* const Spawned = World->SpawnActor<AMD_WallUtility>(
			Slot.UtilityClass,
			Slot.UtilityRelativeTransform * GetActorTransform(),
			SpawnParams);

		if (Spawned)
		{
			ActiveUtilities.Add(Spawned);
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("MD_TutorialWallSpawner: 슬롯 %s 인프라 스폰 실패"),
				*Slot.SlotName.ToString());
		}
	}

	// 인프라가 없으면 제한할 근거가 없으므로 아예 걸지 않는다.
	if (Slot.bCutOnlyAtUtility && ActiveUtilities.Num() > 0)
	{
		Wall->CutLocationFilter.BindUObject(this, &AMD_TutorialWallSpawner::IsCutAllowedAtHit);
	}
	else if (Slot.bCutOnlyAtUtility)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("MD_TutorialWallSpawner: 슬롯 %s 는 인프라 자리 제한이 켜져 있는데 "
				 "인프라를 찾지 못했다 — 어디든 잘린다"),
			*Slot.SlotName.ToString());
	}

	return true;
}

void AMD_TutorialWallSpawner::CollectUtilitiesFromWall(AActor* Wall)
{
	UWorld* const World = GetWorld();
	if (!Wall || !World)
	{
		return;
	}

	// 벽 BP 가 인프라를 ChildActorComponent 로 품고 있으면(BP_MD_CuttableWall_InPipe / _InWire)
	// 벽이 파괴될 때 자식 액터도 함께 파괴된다. 그러면 배관이 터진 뒤 수리할 대상이 없다.
	//
	// 그래서 슬롯 시작 시점에 자식을 걷어내고 같은 자리에 독립 액터로 다시 세운다.
	// 이렇게 하면 벽과 수명이 끊기고, 인프라가 BeginPlay 에서 겹친 벽을 찾아
	// 철거 델리게이트를 구독하는 원래 경로도 그대로 살아난다 (벽이 이미 존재하므로).
	TArray<UChildActorComponent*> ChildComponents;
	Wall->GetComponents<UChildActorComponent>(ChildComponents);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (UChildActorComponent* const Component : ChildComponents)
	{
		const AActor* const Child = Component->GetChildActor();
		if (!Cast<AMD_WallUtility>(Child))
		{
			continue;
		}

		const TSubclassOf<AActor> ChildClass = Component->GetChildActorClass();
		const FTransform ChildTransform = Child->GetActorTransform();

		// 공개 API 로 자식을 정리한다. 컴포넌트의 ChildActor 포인터는 밖에서 손댈 수 없다.
		Component->SetChildActorClass(nullptr);

		AMD_WallUtility* const Promoted =
			World->SpawnActor<AMD_WallUtility>(ChildClass, ChildTransform, SpawnParams);
		if (Promoted)
		{
			ActiveUtilities.Add(Promoted);
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Tutorial] 인프라 승격 실패 (%s) — 이 자리는 수리할 수 없다"),
				ChildClass ? *ChildClass->GetName() : TEXT("null"));
		}
	}

	if (ActiveUtilities.Num() > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[Tutorial] 인프라 %d개를 독립 액터로 승격"),
			ActiveUtilities.Num());
	}
}

void AMD_TutorialWallSpawner::ReleaseSlotActors()
{
	if (AMD_CuttableWall* Wall = ActiveWall.Get())
	{
		Wall->CutLocationFilter.Unbind();
		Wall->OnWallDemolished.RemoveDynamic(this, &AMD_TutorialWallSpawner::HandleWallDemolished);
		Wall->OnWallScanned.RemoveDynamic(this, &AMD_TutorialWallSpawner::HandleWallScanned);
	}

	// 따로 스폰한 인프라는 벽이 사라져도 남는다. 슬롯마다 같은 자리에 벽이 다시 스폰되므로
	// 여기서 치우지 않으면 다음 슬롯의 배관·전선이 앞 슬롯 것과 겹쳐 박힌다.
	// 벽이 품고 있던 ChildActor 인프라는 벽과 함께 사라지므로 손대지 않는다.
	// 인프라는 모두 독립 액터라 벽과 함께 사라지지 않는다. 슬롯이 끝날 때 직접 치운다.
	// (수리 단계를 통과했다면 그때 이미 비워져 있다)
	DestroyActiveUtilities();

	// 자르지 않고 슬롯이 끝난 경우를 위한 정리. 잘린 벽은 스스로 파괴되므로 여기선 null 이다.
	if (AMD_CuttableWall* const Wall = ActiveWall.Get())
	{
		Wall->Destroy();
	}

	ActiveWall.Reset();
	ActiveZone.Reset();
	CapturedPieces.Reset();
	PieceSearchRadius = 0.0f;
}

//~ 폴링 -----------------------------------------------------------------------

void AMD_TutorialWallSpawner::SetPollingEnabled(const bool bEnabled)
{
	FTimerManager& Timers = GetWorldTimerManager();
	if (!bEnabled)
	{
		Timers.ClearTimer(PollTimerHandle);
		return;
	}

	if (!Timers.IsTimerActive(PollTimerHandle))
	{
		Timers.SetTimer(
			PollTimerHandle, this, &AMD_TutorialWallSpawner::PollActivePhase,
			FMath::Max(MinPollIntervalSeconds, PollInterval), true);
	}
}

AActor* AMD_TutorialWallSpawner::FindZoneByTag(const FName ZoneTag) const
{
	if (ZoneTag.IsNone())
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(ZoneTag))
		{
			return *It;
		}
	}
	return nullptr;
}

bool AMD_TutorialWallSpawner::IsAnyPlayerInsideZone(const AActor& Zone) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// 협동으로 들어와도 누구든 도달하면 넘어간다. 서버라 모든 플레이어가 보인다.
	const FBox Bounds = Zone.GetComponentsBoundingBox(true);
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (Pawn && Bounds.IsInsideOrOn(Pawn->GetActorLocation()))
		{
			return true;
		}
	}
	return false;
}

void AMD_TutorialWallSpawner::PollActivePhase()
{
	if (IsActivePhase(EMD_TutorialWallPhase::ReachZone))
	{
		const AActor* Zone = ActiveZone.Get();
		if (Zone && IsAnyPlayerInsideZone(*Zone))
		{
			AdvanceStep();
		}
		return;
	}

	if (IsActivePhase(EMD_TutorialWallPhase::Collect))
	{
		// 튜토리얼 구간에는 조각벽 말고 담을 것이 없으므로 내용물 유무만 본다.
		for (TActorIterator<AMD_TrashBin> It(GetWorld()); It; ++It)
		{
			if (It->GetContentCount() > 0)
			{
				AdvanceStep();
				return;
			}
		}
		return;
	}

	if (IsActivePhase(EMD_TutorialWallPhase::Repair))
	{
		// AMD_WallUtility에는 상태 델리게이트가 없어 상태를 직접 본다.
		// 배관이 여러 개일 수 있으므로(InPipe 는 4개) 파손된 것이 하나도 남지 않아야 넘어간다.
		// 하나라도 수리된 이력이 있어야 하므로 Repaired 가 최소 하나는 있어야 한다.
		bool bAnyBroken = false;
		bool bAnyRepaired = false;
		for (const TWeakObjectPtr<AMD_WallUtility>& Weak : ActiveUtilities)
		{
			if (const AMD_WallUtility* const Utility = Weak.Get())
			{
				const EMD_UtilityState State = Utility->GetUtilityState();
				bAnyBroken |= State == EMD_UtilityState::Broken;
				bAnyRepaired |= State == EMD_UtilityState::Repaired;
			}
		}

		if (!bAnyBroken && bAnyRepaired)
		{
			const FMD_TutorialWallSlot* Slot = GetActiveSlot();
			if (Slot && Slot->bDestroyPiecesOnRepair)
			{
				DestroyCapturedPieces();
			}

			// 수리를 끝냈으면 인프라도 치운다. 벽이 이미 사라진 자리에 배관만 떠 있으면
			// 다음 슬롯의 벽이 그 위에 겹쳐 스폰된다.
			UE_LOG(LogTemp, Log, TEXT("[Tutorial] 수리 완료 — 인프라 %d개 정리"),
				ActiveUtilities.Num());
			DestroyActiveUtilities();

			AdvanceStep();
		}
	}
}

void AMD_TutorialWallSpawner::DestroyActiveUtilities()
{
	for (const TWeakObjectPtr<AMD_WallUtility>& Utility : ActiveUtilities)
	{
		if (AMD_WallUtility* const Resolved = Utility.Get())
		{
			Resolved->Destroy();
		}
	}
	ActiveUtilities.Reset();
}

//~ 기존 시스템 신호 ------------------------------------------------------------

void AMD_TutorialWallSpawner::HandleWallDemolished(AMD_CuttableWall* Wall, AActor* Cutter)
{
	if (!HasAuthority() || Wall != ActiveWall.Get())
	{
		return;
	}

	// 이 방송은 조각이 스폰되기 **전**이다 (CompleteCut).
	// 벽은 곧 숨겨지고 파괴되므로 위치를 지금 붙잡아 두고 다음 틱에 조각을 줍는다.
	const FMD_TutorialWallSlot* Slot = GetActiveSlot();
	if (Slot && Slot->bDestroyPiecesOnRepair)
	{
		FVector Extent;
		Wall->GetActorBounds(false, PieceSearchOrigin, Extent);
		PieceSearchRadius = Extent.Size() + PieceSearchSlackCm;
		GetWorldTimerManager().SetTimerForNextTick(
			this, &AMD_TutorialWallSpawner::CaptureSpawnedPieces);
	}

	// 인프라는 슬롯 시작 때 이미 독립 액터로 승격돼 있으므로(CollectUtilitiesFromWall)
	// 벽은 평소대로 파괴되고 배관·전선만 남는다. 여기서 벽 수명에 손대지 않는다.

	if (IsActivePhase(EMD_TutorialWallPhase::Cut))
	{
		AdvanceStep();
	}
}

void AMD_TutorialWallSpawner::HandleWallScanned(AMD_CuttableWall* Wall, float Duration, AActor* Scanner)
{
	// 스캔 어빌리티는 로컬 조종 머신에서만 Execute_OnScanned를 부른다.
	// 즉 이 신호는 리슨 서버(호스트) 또는 싱글에서만 서버에 도달한다 —
	// 데디케이티드 구성에서 튜토리얼을 돌릴 계획이 생기면 별도 경로가 필요하다.
	if (!HasAuthority()
		|| Wall != ActiveWall.Get()
		|| !IsActivePhase(EMD_TutorialWallPhase::Scan))
	{
		return;
	}

	AdvanceStep();
}

void AMD_TutorialWallSpawner::HandleTrashSorted(
	TSubclassOf<AActor> ItemClass,
	const bool bCorrectSort,
	FGameplayTag SorterCategory)
{
	const FMD_TutorialWallSlot* Slot = GetActiveSlot();

	// 통에 담는 단계(Collect)에서 정분류가 들어오는 것은 통을 안 쓰고 손으로 옮긴 경우다.
	// 그때도 배운 것은 같으므로 Collect 와 Sort 를 함께 통과시킨다.
	// 이 둘을 함께 받지 않으면, 통 없이 분류한 플레이어는 Collect 에 영구히 갇힌다.
	const bool bSortRelevantPhase =
		IsActivePhase(EMD_TutorialWallPhase::Sort)
		|| IsActivePhase(EMD_TutorialWallPhase::Collect);

	if (!HasAuthority() || !Slot || !bSortRelevantPhase)
	{
		return;
	}

	if (!ItemClass || !ItemClass->IsChildOf(AMD_WallPiece::StaticClass()))
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[Tutorial] 분류됨: 조각벽이 아니라 무시 (%s)"),
			ItemClass ? *ItemClass->GetName() : TEXT("null"));
		return;
	}

	// 오분류는 진행시키지 않는다. 감점은 분류기가 이미 넣었으므로 여기서는 안내만 한다.
	if (!bCorrectSort)
	{
		UE_LOG(LogTemp, Log, TEXT("[Tutorial] 조각벽 오분류 (분류기 칸 %s)"),
			*SorterCategory.ToString());
		ShowNotice((ScriptData && !ScriptData->MisSortNotice.IsEmpty())
			? ScriptData->MisSortNotice
			: LOCTEXT("MisSort", "칸이 다릅니다. 안내한 칸에 넣어보세요."));
		return;
	}

	if (Slot->ExpectedSortCategory.IsValid()
		&& !SorterCategory.MatchesTagExact(Slot->ExpectedSortCategory))
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Tutorial] 조각벽이 다른 칸에 정분류됨 — 기대 %s / 실제 %s"),
			*Slot->ExpectedSortCategory.ToString(), *SorterCategory.ToString());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[Tutorial] 조각벽 정분류 확인 (%s) — 단계 진행"),
		*SorterCategory.ToString());

	// Collect 에서 곧바로 통과하면 Sort 단계 문구가 화면에 뜰 틈이 없다.
	// 그 안에 "정분류는 점수, 오분류는 점수와 돈 차감" 규칙이 들어 있어서
	// 통을 안 쓴 플레이어만 그 설명을 못 보고 지나가게 된다. 미리 챙겨 둔다.
	FText SkippedSortBody;
	if (IsActivePhase(EMD_TutorialWallPhase::Collect))
	{
		const FMD_TutorialGuideText* const FromScript = ScriptData
			? ScriptData->FindGuideText(Slot->SlotName, EMD_TutorialWallPhase::Sort)
			: nullptr;
		if (FromScript)
		{
			SkippedSortBody = FromScript->Body;
		}
		else
		{
			for (const FMD_TutorialGuideStep& Step : Slot->Steps)
			{
				if (Step.Phase == EMD_TutorialWallPhase::Sort)
				{
					SkippedSortBody = Step.Body;
					break;
				}
			}
		}
	}

	// Collect 에서 들어왔으면 Sort 까지 한 번에 통과시킨다. 슬롯이 끝나면 루프도 끝난다.
	for (int32 Advance = 0; Advance < MaxChainedStepAdvances; ++Advance)
	{
		if (!IsActivePhase(EMD_TutorialWallPhase::Collect)
			&& !IsActivePhase(EMD_TutorialWallPhase::Sort))
		{
			break;
		}
		AdvanceStep();
	}

	ShowNotice(SkippedSortBody);
}

bool AMD_TutorialWallSpawner::IsCutAllowedAtHit(const FHitResult* WallHit)
{
	const FMD_TutorialWallSlot* Slot = GetActiveSlot();
	if (!Slot || !Slot->bCutOnlyAtUtility)
	{
		return true;
	}

	// 인프라가 모두 사라졌으면 제한을 유지할 근거가 없다. 막아두면 진행이 죽는다.
	bool bAnyUtility = false;
	const float Tolerance = FMath::Max(0.0f, Slot->UtilityCutTolerance);
	for (const TWeakObjectPtr<AMD_WallUtility>& Weak : ActiveUtilities)
	{
		const AMD_WallUtility* const Utility = Weak.Get();
		if (!Utility)
		{
			continue;
		}
		bAnyUtility = true;

		// 배관이 여러 개면 그중 아무 하나의 자리라도 맞으면 허용한다.
		if (WallHit && WallHit->bBlockingHit
			&& Utility->GetComponentsBoundingBox(true)
				.ExpandBy(Tolerance)
				.IsInsideOrOn(WallHit->ImpactPoint))
		{
			return true;
		}
	}

	if (!bAnyUtility)
	{
		return true;
	}

	// 경고 문구도 대본 에셋이 갖고 있으면 그쪽이 원본이다.
	const FMD_TutorialGuideText* const Override = FindActiveGuideText();
	ShowNotice((Override && !Override->BlockedMessage.IsEmpty())
		? Override->BlockedMessage
		: Slot->BlockedCutMessage);
	return false;
}

//~ 조각 정리 ------------------------------------------------------------------

void AMD_TutorialWallSpawner::CaptureSpawnedPieces()
{
	CapturedPieces.Reset();
	if (PieceSearchRadius <= 0.0f)
	{
		return;
	}

	const float RadiusSquared = FMath::Square(PieceSearchRadius);
	for (TActorIterator<AMD_WallPiece> It(GetWorld()); It; ++It)
	{
		AMD_WallPiece* Piece = *It;
		if (FVector::DistSquared(Piece->GetActorLocation(), PieceSearchOrigin) <= RadiusSquared)
		{
			CapturedPieces.Add(Piece);
		}
	}
}

void AMD_TutorialWallSpawner::DestroyCapturedPieces()
{
	for (const TWeakObjectPtr<AMD_WallPiece>& Piece : CapturedPieces)
	{
		if (Piece.IsValid())
		{
			Piece->Destroy();
		}
	}
	CapturedPieces.Reset();
}

//~ 조회·표시 ------------------------------------------------------------------

const FMD_TutorialWallSlot* AMD_TutorialWallSpawner::GetActiveSlot() const
{
	return Slots.IsValidIndex(CurrentSlotIndex) ? &Slots[CurrentSlotIndex] : nullptr;
}

bool AMD_TutorialWallSpawner::IsPlayingIntro() const
{
	// 슬롯이 없는데 단계가 있으면 도입부다. 둘 다 없으면 시작 전이다.
	return CurrentSlotIndex == INDEX_NONE && GetIntroPages().IsValidIndex(CurrentStepIndex);
}

const FMD_TutorialGuideStep* AMD_TutorialWallSpawner::GetActiveStep() const
{
	if (IsPlayingIntro())
	{
		return &GetIntroPages()[CurrentStepIndex];
	}

	const FMD_TutorialWallSlot* Slot = GetActiveSlot();
	return (Slot && Slot->Steps.IsValidIndex(CurrentStepIndex))
		? &Slot->Steps[CurrentStepIndex]
		: nullptr;
}

bool AMD_TutorialWallSpawner::IsActivePhase(const EMD_TutorialWallPhase Phase) const
{
	const FMD_TutorialGuideStep* Step = GetActiveStep();
	return Step && Step->Phase == Phase;
}

bool AMD_TutorialWallSpawner::GetCurrentGuidance(FText& OutTitle, FText& OutBody) const
{
	if (const FMD_TutorialGuideStep* Step = GetActiveStep())
	{
		// 대본 에셋이 이 단계의 문구를 갖고 있으면 그쪽이 원본이다.
		const FMD_TutorialGuideText* const Override = FindActiveGuideText();
		OutTitle = Override ? Override->Title : Step->Title;
		OutBody = Override ? Override->Body : Step->Body;
		return true;
	}

	OutTitle = FText::GetEmpty();
	OutBody = FText::GetEmpty();
	return false;
}

bool AMD_TutorialWallSpawner::GetCurrentSpeaker(FText& OutSpeaker, bool& bOutIsNarrative) const
{
	if (const FMD_TutorialGuideStep* Step = GetActiveStep())
	{
		const FMD_TutorialGuideText* const Override = FindActiveGuideText();
		OutSpeaker = (Override && !Override->Speaker.IsEmpty())
			? Override->Speaker
			: ResolveSpeaker(*Step);
		bOutIsNarrative = Step->Phase == EMD_TutorialWallPhase::Narrative;
		return true;
	}

	OutSpeaker = FText::GetEmpty();
	bOutIsNarrative = false;
	return false;
}

bool AMD_TutorialWallSpawner::GetSkipHint(FText& OutHint) const
{
	// 넘길 수 있는 건 도입부 서술뿐이다. 조작 안내는 플레이어가 해내야 넘어간다.
	const FText Hint = ResolveSkipHint();
	if (IsPlayingIntro() && SkipIntroKey.IsValid() && !Hint.IsEmpty())
	{
		OutHint = Hint;
		return true;
	}

	OutHint = FText::GetEmpty();
	return false;
}

void AMD_TutorialWallSpawner::BroadcastGuidance() const
{
	FText Title;
	FText Body;
	GetCurrentGuidance(Title, Body);
	OnGuidanceChanged.Broadcast(Title, Body);
}

void AMD_TutorialWallSpawner::ShowNotice(const FText& Message)
{
	if (Message.IsEmpty())
	{
		return;
	}

	// 절단 필터는 절단 중 매 틱 호출된다. 스로틀이 없으면 같은 경고가 도배된다.
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Now - LastNoticeTime < NoticeCooldown)
	{
		return;
	}
	LastNoticeTime = Now;

	PendingNoticeMessage = Message;
	++NoticeSerial;
	ForceNetUpdate();
	// 서버에는 OnRep이 오지 않으므로 직접 부른다 (리슨 서버 호스트 화면용)
	OnRep_Notice();
}

void AMD_TutorialWallSpawner::OnRep_Progress()
{
	BroadcastGuidance();
	// 잠금은 각 머신의 로컬 컨트롤러에 걸어야 한다. 서버는 EnterIntroPage/EnterStep 에서 부른다.
	ApplyMovementLock();
}

void AMD_TutorialWallSpawner::OnRep_Notice()
{
	if (!PendingNoticeMessage.IsEmpty())
	{
		OnNotice.Broadcast(PendingNoticeMessage);
	}
}

#undef LOCTEXT_NAMESPACE

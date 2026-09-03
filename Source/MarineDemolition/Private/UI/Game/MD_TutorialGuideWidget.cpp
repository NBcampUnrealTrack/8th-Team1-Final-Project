#include "UI/Game/MD_TutorialGuideWidget.h"

#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "World/Tutorial/MD_TutorialWallSpawner.h"

namespace
{
	/** 진행기를 찾는 재시도 주기의 하한. 0 이 들어와도 타이머가 폭주하지 않게 막는다 */
	constexpr float MinBindRetryIntervalSeconds = 0.1f;

	/** 경고를 띄워 두는 시간의 하한. 너무 짧으면 읽을 수 없다 */
	constexpr float MinNoticeDisplaySeconds = 0.5f;
}

void UMD_TutorialGuideWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 진행기가 아직 없을 수 있으므로(레벨 스트리밍·늦은 스폰) 붙을 때까지 다시 시도한다.
	// 붙기 전에는 접혀 있어야 빈 패널이 보이지 않는다.
	ApplyGuidance(FText::GetEmpty(), FText::GetEmpty(), false);
	HideNotice();
	TryBindSpawner();
}

void UMD_TutorialGuideWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BindRetryTimerHandle);
		World->GetTimerManager().ClearTimer(NoticeHideTimerHandle);
	}

	if (AMD_TutorialWallSpawner* Spawner = BoundSpawner.Get())
	{
		Spawner->OnGuidanceChanged.RemoveDynamic(this, &UMD_TutorialGuideWidget::HandleGuidanceChanged);
		Spawner->OnNotice.RemoveDynamic(this, &UMD_TutorialGuideWidget::HandleNotice);
	}
	BoundSpawner.Reset();

	Super::NativeDestruct();
}

void UMD_TutorialGuideWidget::TryBindSpawner()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (BoundSpawner.IsValid())
	{
		World->GetTimerManager().ClearTimer(BindRetryTimerHandle);
		return;
	}

	AMD_TutorialWallSpawner* Spawner = Cast<AMD_TutorialWallSpawner>(
		UGameplayStatics::GetActorOfClass(World, AMD_TutorialWallSpawner::StaticClass()));

	if (!Spawner)
	{
		if (!World->GetTimerManager().IsTimerActive(BindRetryTimerHandle))
		{
			World->GetTimerManager().SetTimer(
				BindRetryTimerHandle, this, &UMD_TutorialGuideWidget::TryBindSpawner,
				FMath::Max(MinBindRetryIntervalSeconds, SpawnerBindRetryInterval), true);
		}
		return;
	}

	World->GetTimerManager().ClearTimer(BindRetryTimerHandle);
	BoundSpawner = Spawner;
	Spawner->OnGuidanceChanged.AddDynamic(this, &UMD_TutorialGuideWidget::HandleGuidanceChanged);
	Spawner->OnNotice.AddDynamic(this, &UMD_TutorialGuideWidget::HandleNotice);

	// 위젯이 진행 도중에 만들어졌을 수 있으므로 현재 상태를 한 번 끌어온다.
	FText Title;
	FText Body;
	const bool bHasGuidance = Spawner->GetCurrentGuidance(Title, Body);
	ApplyGuidance(Title, Body, bHasGuidance);
}

void UMD_TutorialGuideWidget::HandleGuidanceChanged(const FText& Title, const FText& Body)
{
	ApplyGuidance(Title, Body, !Title.IsEmpty() || !Body.IsEmpty());
}

void UMD_TutorialGuideWidget::ApplyGuidance(const FText& Title, const FText& Body, const bool bHasGuidance)
{
	SetVisibility(bHasGuidance ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);

	if (TB_Title)
	{
		TB_Title->SetText(Title);
	}
	if (TB_Body)
	{
		TB_Body->SetText(Body);
	}

	// 화자·서술 여부·넘기기 힌트는 델리게이트 인자에 없다 — 진행기에서 직접 읽는다.
	FText Speaker;
	FText SkipHint;
	bool bIsNarrative = false;
	bool bCanSkip = false;
	if (const AMD_TutorialWallSpawner* Spawner = BoundSpawner.Get())
	{
		Spawner->GetCurrentSpeaker(Speaker, bIsNarrative);
		bCanSkip = Spawner->GetSkipHint(SkipHint);
	}

	if (TB_SkipHint)
	{
		TB_SkipHint->SetText(SkipHint);
		TB_SkipHint->SetVisibility(bCanSkip
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed);
	}

	if (TB_Speaker)
	{
		TB_Speaker->SetText(Speaker);
		// 조작 안내에는 화자가 없다. 빈 이름표가 자리를 먹지 않게 접는다.
		TB_Speaker->SetVisibility(Speaker.IsEmpty()
			? ESlateVisibility::Collapsed
			: ESlateVisibility::HitTestInvisible);
	}

	OnGuidanceUpdated(Title, Body, bHasGuidance, bIsNarrative);
}

void UMD_TutorialGuideWidget::HandleNotice(const FText& Message)
{
	if (Message.IsEmpty())
	{
		return;
	}

	if (TB_Notice)
	{
		TB_Notice->SetText(Message);
	}
	if (Panel_Notice)
	{
		Panel_Notice->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else if (TB_Notice)
	{
		TB_Notice->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	OnNoticeShown(Message);

	if (UWorld* World = GetWorld())
	{
		// 새 경고가 오면 표시 시간을 처음부터 다시 센다.
		World->GetTimerManager().SetTimer(
			NoticeHideTimerHandle, this, &UMD_TutorialGuideWidget::HideNotice,
			FMath::Max(MinNoticeDisplaySeconds, NoticeDisplaySeconds), false);
	}
}

void UMD_TutorialGuideWidget::HideNotice()
{
	if (Panel_Notice)
	{
		Panel_Notice->SetVisibility(ESlateVisibility::Collapsed);
	}
	else if (TB_Notice)
	{
		TB_Notice->SetVisibility(ESlateVisibility::Collapsed);
	}
}

#include "Audio/MD_AudioSubsystem.h"

#include "Data/Audio/MD_AudioCategoryData.h"
#include "Data/Audio/MD_AudioSettings.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"
#include "SubmixEffects/SubmixEffectFilter.h"

namespace
{
	/** 조건 변화로 켜고 끌 때의 페이드 길이(초). 물에 들어가고 나오는 전환용 */
	constexpr float FadeSecondsOnConditionChange = 0.4f;

	/** md.Audio.Debug 1 로 깊이·컷오프·태그를 화면에 찍는다 */
	static int32 GMDAudioDebug = 0;
	static FAutoConsoleVariableRef CVarMDAudioDebug(
		TEXT("md.Audio.Debug"),
		GMDAudioDebug,
		TEXT("MD 오디오 디버그 표시. 1 = 화면에 깊이/컷오프/환경 태그 출력"),
		ECVF_Cheat);
}

namespace MD_AudioPrivate
{
	static FGameplayTag GetAmbienceRootTag()
	{
		static const FGameplayTag Tag =
			FGameplayTag::RequestGameplayTag(TEXT("SFX.Ambience"), /*ErrorIfNotFound*/ false);
		return Tag;
	}

	/** 앰비언스는 수심 피치만 쓰고 LPF는 건너뛴다 */
	bool ShouldApplyDepthFilter(const FMD_AudioEntry& Entry)
	{
		if (Entry.Space != EMD_AudioSpace::Underwater)
		{
			return false;
		}

		if (Entry.bDepthPitch)
		{
			return false;
		}

		const FGameplayTag AmbienceRoot = GetAmbienceRootTag();
		return !AmbienceRoot.IsValid() || !Entry.EventTag.MatchesTag(AmbienceRoot);
	}

	bool ShouldApplyDepthFilter(bool bUnderwater, bool bDepthPitch, const FGameplayTag& EventTag)
	{
		if (!bUnderwater)
		{
			return false;
		}

		if (bDepthPitch)
		{
			return false;
		}

		const FGameplayTag AmbienceRoot = GetAmbienceRootTag();
		return !AmbienceRoot.IsValid() || !EventTag.MatchesTag(AmbienceRoot);
	}

	bool ShouldApplyDepthPitch(bool bUnderwater, bool bDepthPitch)
	{
		return bUnderwater && bDepthPitch;
	}

	/** 옥타브(로그) 공간 보간. 사람 지각이 로그라 깊이 변화가 균일하게 들린다 */
	float BlendCutoff(float Surface, float Deep, float Alpha, bool bLogarithmic)
	{
		Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);

		if (!bLogarithmic || Surface <= 0.0f || Deep <= 0.0f)
		{
			return FMath::Lerp(Surface, Deep, Alpha);
		}

		const float LogSurface = FMath::Loge(Surface);
		const float LogDeep = FMath::Loge(Deep);
		return FMath::Exp(FMath::Lerp(LogSurface, LogDeep, Alpha));
	}
}

// ------------------------------------------------------------------ 라이프사이클

bool UMD_AudioSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	// 데디케이티드 서버는 사운드를 재생하지 않는다
	const UWorld* World = Cast<UWorld>(Outer);
	return World == nullptr || World->GetNetMode() != NM_DedicatedServer;
}

void UMD_AudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	RebuildIndex();

	const UMD_AudioSettings& Settings = UMD_AudioSettings::Get();
	CurrentCutoffHz = Settings.CutoffAtSurface;
	CachedFilter = Settings.UnderwaterFilter.LoadSynchronous();

	if (!CachedFilter)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MD_Audio] 수중 필터 프리셋이 설정되지 않았습니다. Project Settings → Game → MD Audio 에서 지정하세요."));
	}
}

void UMD_AudioSubsystem::Deinitialize()
{
	for (const FActiveSound& Active : ActiveSounds)
	{
		if (UAudioComponent* Component = Active.Component.Get())
		{
			Component->Stop();
		}
	}
	ActiveSounds.Reset();
	EntriesByTag.Reset();
	CachedFilter = nullptr;

	Super::Deinitialize();
}

TStatId UMD_AudioSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMD_AudioSubsystem, STATGROUP_Tickables);
}

void UMD_AudioSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateUnderwaterCutoff(DeltaTime);
	EnforceLoopConditions();
	PruneFinished();
	DrawDebugInfo();
}

// ---------------------------------------------------------------------- 인덱스

void UMD_AudioSubsystem::RebuildIndex()
{
	EntriesByTag.Reset();
	bIndexBuilt = false;

	const UMD_AudioSettings& Settings = UMD_AudioSettings::Get();
	UMD_AudioLibrary* Library = Settings.AudioLibrary.LoadSynchronous();
	if (!Library)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MD_Audio] AudioLibrary 가 설정되지 않아 이벤트 인덱스가 비었습니다."));
		return;
	}

	TArray<FMD_AudioEntry> AllEntries;
	Library->CollectEntries(AllEntries);

	for (const FMD_AudioEntry& Entry : AllEntries)
	{
		if (!Entry.IsValidEntry())
		{
			UE_LOG(LogTemp, Warning, TEXT("[MD_Audio] 불완전한 항목을 건너뜁니다: %s"),
				*Entry.EventTag.ToString());
			continue;
		}

		EntriesByTag.FindOrAdd(Entry.EventTag).Add(Entry);
	}

	// 구체적인 조건이 먼저 오도록 정렬해 두면 선택이 단순해진다
	for (TPair<FGameplayTag, TArray<FMD_AudioEntry>>& Pair : EntriesByTag)
	{
		Pair.Value.Sort([](const FMD_AudioEntry& A, const FMD_AudioEntry& B)
		{
			return A.GetSpecificity() > B.GetSpecificity();
		});
	}

	bIndexBuilt = true;
	UE_LOG(LogTemp, Log, TEXT("[MD_Audio] 이벤트 %d개 / 정의 %d건 인덱싱"),
		EntriesByTag.Num(), AllEntries.Num());
}

const FMD_AudioEntry* UMD_AudioSubsystem::ResolveEntry(const FGameplayTag& EventTag) const
{
	const TArray<FMD_AudioEntry>* Candidates = EntriesByTag.Find(EventTag);
	if (!Candidates)
	{
		return nullptr;
	}

	// 이미 구체성 내림차순이라 조건을 만족하는 첫 항목이 최적이다
	for (const FMD_AudioEntry& Entry : *Candidates)
	{
		if (PassesConditions(Entry))
		{
			return &Entry;
		}
	}

	return nullptr;
}

bool UMD_AudioSubsystem::PassesConditions(const FMD_AudioEntry& Entry) const
{
	if (Entry.RequiredStateTags.Num() > 0 && !ActiveStateTags.HasAll(Entry.RequiredStateTags))
	{
		return false;
	}

	if (Entry.BlockedStateTags.Num() > 0 && ActiveStateTags.HasAny(Entry.BlockedStateTags))
	{
		return false;
	}

	if (Entry.RequiredEnvironmentTags.Num() > 0 && !ActiveEnvironmentTags.HasAny(Entry.RequiredEnvironmentTags))
	{
		return false;
	}

	if (Entry.BlockedEnvironmentTags.Num() > 0 && ActiveEnvironmentTags.HasAny(Entry.BlockedEnvironmentTags))
	{
		return false;
	}

	return true;
}

// ---------------------------------------------------------------------- 재생

FMD_AudioHandle UMD_AudioSubsystem::PlayEventAtLocation(const FGameplayTag& EventTag, const FVector& Location)
{
	if (const FMD_AudioEntry* Entry = ResolveEntry(EventTag))
	{
		return PlayResolved(*Entry, Location, nullptr, NAME_None, false);
	}

	UE_LOG(LogTemp, Verbose, TEXT("[MD_Audio] 정의 없음: %s"), *EventTag.ToString());
	return FMD_AudioHandle();
}

FMD_AudioHandle UMD_AudioSubsystem::PlayEventAttached(const FGameplayTag& EventTag, USceneComponent* AttachTo, FName SocketName)
{
	if (const FMD_AudioEntry* Entry = ResolveEntry(EventTag))
	{
		const FVector Location = AttachTo ? AttachTo->GetComponentLocation() : FVector::ZeroVector;
		return PlayResolved(*Entry, Location, AttachTo, SocketName, false);
	}

	return FMD_AudioHandle();
}

FMD_AudioHandle UMD_AudioSubsystem::PlayEvent2D(const FGameplayTag& EventTag)
{
	if (const FMD_AudioEntry* Entry = ResolveEntry(EventTag))
	{
		return PlayResolved(*Entry, FVector::ZeroVector, nullptr, NAME_None, true);
	}

	return FMD_AudioHandle();
}

UAudioComponent* UMD_AudioSubsystem::SpawnForEntry(const FMD_AudioEntry& Entry, const FVector& Location,
	USceneComponent* AttachTo, FName SocketName, bool bForce2D)
{
	UWorld* World = GetWorld();
	USoundBase* Sound = Entry.Sound.LoadSynchronous();
	if (!World || !Sound)
	{
		return nullptr;
	}

	// 서브믹스 라우팅은 사운드 에셋에 이미 지정되어 있다(SM_MD_Underwater / SM_MD_Dry).
	// 런타임에 SoundSubmixObject 를 덮어쓰면 에디터에서 에셋이 더티가 되므로 건드리지 않는다.
	// Entry.Space 는 데이터 검증과 일괄 라우팅 스크립트가 참조하는 선언적 정보다.

	USoundAttenuation* Attenuation = Entry.Attenuation.LoadSynchronous();
	USoundConcurrency* Concurrency = Entry.Concurrency.LoadSynchronous();

	const bool bPlay2D = bForce2D || Entry.b2D || Entry.Space == EMD_AudioSpace::UI;

	UAudioComponent* Component = nullptr;

	if (bPlay2D)
	{
		// 보이스가 시작된 뒤에 필터를 켜면 반영되지 않는 경우가 있다.
		// 컴포넌트를 먼저 만들고 필터를 설정한 다음 재생한다.
		Component = UGameplayStatics::CreateSound2D(
			World, Sound, Entry.VolumeMultiplier, Entry.PitchMultiplier,
			0.0f, Concurrency, /*bPersistAcrossLevelTransition*/ false, /*bAutoDestroy*/ !Entry.bLooping);

		if (Component && MD_AudioPrivate::ShouldApplyDepthFilter(Entry))
		{
			Component->bEnableLowPassFilter = true;
			Component->LowPassFilterFrequency = CurrentCutoffHz;
		}
		else if (Component && MD_AudioPrivate::ShouldApplyDepthPitch(
			Entry.Space == EMD_AudioSpace::Underwater, Entry.bDepthPitch))
		{
			Component->SetPitchMultiplier(Entry.PitchMultiplier * CurrentDepthPitch);
		}

		if (Component)
		{
			Component->Play();
		}
	}
	else if (AttachTo)
	{
		Component = UGameplayStatics::SpawnSoundAttached(
			Sound, AttachTo, SocketName, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset,
			/*bStopWhenAttachedToDestroyed*/ true, Entry.VolumeMultiplier, Entry.PitchMultiplier,
			0.0f, Attenuation, Concurrency, /*bAutoDestroy*/ !Entry.bLooping);
	}
	else
	{
		Component = UGameplayStatics::SpawnSoundAtLocation(
			World, Sound, Location, FRotator::ZeroRotator,
			Entry.VolumeMultiplier, Entry.PitchMultiplier, 0.0f,
			Attenuation, Concurrency, /*bAutoDestroy*/ !Entry.bLooping);
	}

	if (MD_AudioPrivate::ShouldApplyDepthFilter(Entry))
	{
		ApplyUnderwaterFilter(Component);
	}
	else if (Component && MD_AudioPrivate::ShouldApplyDepthPitch(
		Entry.Space == EMD_AudioSpace::Underwater, Entry.bDepthPitch))
	{
		Component->SetPitchMultiplier(Entry.PitchMultiplier * CurrentDepthPitch);
	}

	return Component;
}

FMD_AudioHandle UMD_AudioSubsystem::PlayResolved(const FMD_AudioEntry& Entry, const FVector& Location,
	USceneComponent* AttachTo, FName SocketName, bool bForce2D)
{
	UAudioComponent* Component = SpawnForEntry(Entry, Location, AttachTo, SocketName, bForce2D);

	// 루프가 아니면 핸들을 발급하지 않는다. 원샷은 끝나면 스스로 사라진다
	if (!Entry.bLooping)
	{
		return FMD_AudioHandle();
	}

	FMD_AudioHandle Handle;
	Handle.Id = NextHandleId++;

	FActiveSound Active;
	Active.Id = Handle.Id;
	Active.EventTag = Entry.EventTag;
	Active.Component = Component;
	Active.bAutoManage = Entry.bStopWhenConditionsFail;
	Active.bUnderwater = (Entry.Space == EMD_AudioSpace::Underwater);
	Active.bDepthPitch = Entry.bDepthPitch;
	Active.BasePitch = Entry.PitchMultiplier;
	Active.AttachTo = AttachTo;
	Active.SocketName = SocketName;
	Active.Location = Location;
	Active.bForce2D = bForce2D;
	ActiveSounds.Add(Active);

	// 조건이 아직 성립하지 않아 스폰이 실패했어도 요청은 남긴다.
	// 물에 들어가면 EnforceLoopConditions 가 알아서 시작한다.
	return Handle;
}

void UMD_AudioSubsystem::StopHandle(const FMD_AudioHandle& Handle, float FadeOutSeconds)
{
	if (!Handle.IsValid())
	{
		return;
	}

	for (int32 Index = ActiveSounds.Num() - 1; Index >= 0; --Index)
	{
		if (ActiveSounds[Index].Id != Handle.Id)
		{
			continue;
		}

		if (UAudioComponent* Component = ActiveSounds[Index].Component.Get())
		{
			if (FadeOutSeconds > 0.0f)
			{
				Component->FadeOut(FadeOutSeconds, 0.0f);
			}
			else
			{
				Component->Stop();
			}
		}

		ActiveSounds.RemoveAtSwap(Index);
		return;
	}
}

void UMD_AudioSubsystem::StopEvent(const FGameplayTag& EventTag, float FadeOutSeconds)
{
	for (int32 Index = ActiveSounds.Num() - 1; Index >= 0; --Index)
	{
		if (ActiveSounds[Index].EventTag != EventTag)
		{
			continue;
		}

		if (UAudioComponent* Component = ActiveSounds[Index].Component.Get())
		{
			if (FadeOutSeconds > 0.0f)
			{
				Component->FadeOut(FadeOutSeconds, 0.0f);
			}
			else
			{
				Component->Stop();
			}
		}

		ActiveSounds.RemoveAtSwap(Index);
	}
}

void UMD_AudioSubsystem::PruneFinished()
{
	for (int32 Index = ActiveSounds.Num() - 1; Index >= 0; --Index)
	{
		// 자동관리 요청은 소리가 없어도 유지한다. 조건이 돌아오면 재개해야 한다
		if (ActiveSounds[Index].bAutoManage)
		{
			continue;
		}

		const UAudioComponent* Component = ActiveSounds[Index].Component.Get();
		if (!Component || !Component->IsPlaying())
		{
			ActiveSounds.RemoveAtSwap(Index);
		}
	}
}

// ------------------------------------------------------------------- 상태 · 환경

void UMD_AudioSubsystem::SetStateTag(const FGameplayTag& StateTag, bool bActive)
{
	if (!StateTag.IsValid())
	{
		return;
	}

	if (bActive)
	{
		ActiveStateTags.AddTag(StateTag);
	}
	else
	{
		ActiveStateTags.RemoveTag(StateTag);
	}
}

void UMD_AudioSubsystem::SetEnvironmentTag(const FGameplayTag& EnvironmentTag, bool bActive)
{
	if (!EnvironmentTag.IsValid())
	{
		return;
	}

	if (bActive)
	{
		ActiveEnvironmentTags.AddTag(EnvironmentTag);
	}
	else
	{
		ActiveEnvironmentTags.RemoveTag(EnvironmentTag);
	}
}

// ---------------------------------------------------------------- 수중 컷오프

void UMD_AudioSubsystem::UpdateUnderwaterCutoff(float DeltaTime)
{
	const UMD_AudioSettings& Settings = UMD_AudioSettings::Get();

	CutoffUpdateAccumulator += DeltaTime;
	if (CutoffUpdateAccumulator < Settings.CutoffUpdateInterval)
	{
		return;
	}
	const float Elapsed = CutoffUpdateAccumulator;
	CutoffUpdateAccumulator = 0.0f;

	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APlayerCameraManager* Camera = PC ? PC->PlayerCameraManager : nullptr;
	if (!Camera)
	{
		return;
	}

	// 수면(Z=0, L_Ocean 의 WaterBodyCustom) 아래로 얼마나 내려갔는지
	const float Depth = Settings.WaterSurfaceZ - Camera->GetCameraLocation().Z;
	const float Alpha = FMath::Clamp(Depth / FMath::Max(Settings.MaxDepthCm, 1.0f), 0.0f, 1.0f);

	const float TargetHz = MD_AudioPrivate::BlendCutoff(
		Settings.CutoffAtSurface, Settings.CutoffAtMaxDepth, Alpha, Settings.bLogarithmicCutoffBlend);

	// 급격히 바꾸면 필터가 훑는 소리가 들린다
	CurrentCutoffHz = FMath::FInterpTo(CurrentCutoffHz, TargetHz, Elapsed, Settings.CutoffInterpSpeed);

	const float TargetPitch = FMath::Lerp(Settings.PitchAtSurface, Settings.PitchAtMaxDepth, Alpha);
	CurrentDepthPitch = FMath::FInterpTo(CurrentDepthPitch, TargetPitch, Elapsed, Settings.CutoffInterpSpeed);

	// 서브믹스 프리셋에도 반영해 둔다 (Submix 에디터에서 확인용)
	if (CachedFilter)
	{
		CachedFilter->SetFilterCutoffFrequency(CurrentCutoffHz);
	}

	// 실제 감쇠는 컴포넌트별 로우패스로 적용한다.
	// 서브믹스 이펙트 체인은 런타임 등록 시점 문제로 신뢰할 수 없었다.
	ApplyCutoffToActiveSounds();

	UpdateSurfaceTag(PC);
}

void UMD_AudioSubsystem::UpdateSurfaceTag(const APlayerController* PC)
{
	static const FGameplayTag SurfaceAboveTag =
		FGameplayTag::RequestGameplayTag(TEXT("Env.Surface.Above"), /*ErrorIfNotFound*/ false);
	if (!SurfaceAboveTag.IsValid())
	{
		return;
	}

	// 물속 판정은 카메라 높이가 아니라 캡슐 잠김으로 한다.
	// UMD_OxygenComponent 와 같은 기준을 써서 오디오와 산소가 어긋나지 않게 한다.
	const ACharacter* Character = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
	const UCharacterMovementComponent* Movement =
		Character ? Character->GetCharacterMovement() : nullptr;

	bool bAboveSurface = true;
	if (Movement && Movement->IsInWater())
	{
		const float Threshold = UMD_AudioSettings::Get().SubmergedImmersionThreshold;
		bAboveSurface = Movement->ImmersionDepth() < Threshold;
	}

	if (ActiveEnvironmentTags.HasTagExact(SurfaceAboveTag) != bAboveSurface)
	{
		SetEnvironmentTag(SurfaceAboveTag, bAboveSurface);
	}
}

void UMD_AudioSubsystem::ApplyUnderwaterFilter(UAudioComponent* Component) const
{
	if (!Component)
	{
		return;
	}

	Component->SetLowPassFilterEnabled(true);
	Component->SetLowPassFilterFrequency(CurrentCutoffHz);
}

void UMD_AudioSubsystem::ApplyCutoffToActiveSounds()
{
	LastAppliedCount = 0;
	LastSkippedCount = 0;

	for (const FActiveSound& Active : ActiveSounds)
	{
		UAudioComponent* Component = Active.Component.Get();
		if (!Component)
		{
			++LastSkippedCount;
			continue;
		}

		if (MD_AudioPrivate::ShouldApplyDepthFilter(
			Active.bUnderwater, Active.bDepthPitch, Active.EventTag))
		{
			Component->bEnableLowPassFilter = true;
			Component->SetLowPassFilterFrequency(CurrentCutoffHz);
			++LastAppliedCount;
			continue;
		}

		if (MD_AudioPrivate::ShouldApplyDepthPitch(Active.bUnderwater, Active.bDepthPitch))
		{
			Component->SetPitchMultiplier(Active.BasePitch * CurrentDepthPitch);
			++LastAppliedCount;
			continue;
		}

		++LastSkippedCount;
	}
}

void UMD_AudioSubsystem::DrawDebugInfo()
{
	if (GMDAudioDebug == 0 || GEngine == nullptr)
	{
		return;
	}

	const UMD_AudioSettings& Settings = UMD_AudioSettings::Get();
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APlayerCameraManager* Camera = PC ? PC->PlayerCameraManager : nullptr;
	const ACharacter* Character = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
	const UCharacterMovementComponent* Movement =
		Character ? Character->GetCharacterMovement() : nullptr;

	const float CameraZ = Camera ? Camera->GetCameraLocation().Z : 0.0f;
	const float Depth = Settings.WaterSurfaceZ - CameraZ;
	const float Alpha = FMath::Clamp(Depth / FMath::Max(Settings.MaxDepthCm, 1.0f), 0.0f, 1.0f);

	int32 ActiveLoops = 0;
	for (const FActiveSound& Active : ActiveSounds)
	{
		if (Active.Component.IsValid() && Active.Component->IsPlaying())
		{
			++ActiveLoops;
		}
	}

	auto Line = [](int32 Slot, const FString& InText)
	{
		GEngine->AddOnScreenDebugMessage(0x4D440000 + Slot, 0.0f, FColor::Cyan, InText);
	};

	Line(0, TEXT("[MD Audio]"));
	Line(1, FString::Printf(TEXT("  filter preset : %s"),
		CachedFilter ? *CachedFilter->GetName() : TEXT("NONE!")));
	Line(2, FString::Printf(TEXT("  camera Z      : %.0f  (surface %.0f)"),
		CameraZ, Settings.WaterSurfaceZ));
	Line(3, FString::Printf(TEXT("  depth         : %.0f cm  alpha %.2f  (max %.0f)"),
		Depth, Alpha, Settings.MaxDepthCm));
	Line(4, FString::Printf(TEXT("  cutoff        : %.0f Hz  [%.0f -> %.0f]"),
		CurrentCutoffHz, Settings.CutoffAtSurface, Settings.CutoffAtMaxDepth));
	Line(5, FString::Printf(TEXT("  IsInWater     : %s  immersion %.2f  (thr %.2f)"),
		Movement ? (Movement->IsInWater() ? TEXT("true") : TEXT("false")) : TEXT("(no pawn)"),
		Movement ? Movement->ImmersionDepth() : -1.0f, Settings.SubmergedImmersionThreshold));
	Line(6, FString::Printf(TEXT("  env tags      : %s"),
		ActiveEnvironmentTags.IsEmpty() ? TEXT("(none)") : *ActiveEnvironmentTags.ToStringSimple()));
	Line(7, FString::Printf(TEXT("  state tags    : %s"),
		ActiveStateTags.IsEmpty() ? TEXT("(none)") : *ActiveStateTags.ToStringSimple()));
	Line(8, FString::Printf(TEXT("  loops         : %d / requests %d  events %d"),
		ActiveLoops, ActiveSounds.Num(), EntriesByTag.Num()));
	Line(9, FString::Printf(TEXT("  depth fx      : lpf/pitch applied %d  skipped %d"),
		LastAppliedCount, LastSkippedCount));
	Line(10, FString::Printf(TEXT("  depth pitch   : %.3f  [%.2f -> %.2f]"),
		CurrentDepthPitch, Settings.PitchAtSurface, Settings.PitchAtMaxDepth));
}

void UMD_AudioSubsystem::EnforceLoopConditions()
{
	for (int32 Index = ActiveSounds.Num() - 1; Index >= 0; --Index)
	{
		FActiveSound& Active = ActiveSounds[Index];
		if (!Active.bAutoManage)
		{
			continue;
		}

		const FMD_AudioEntry* Entry = ResolveEntry(Active.EventTag);
		UAudioComponent* Component = Active.Component.Get();

		if (Entry == nullptr)
		{
			// 조건이 깨졌다. 컴포넌트만 정리하고 요청은 남겨 재개를 기다린다
			if (Component)
			{
				Component->FadeOut(FadeSecondsOnConditionChange, 0.0f);
				Active.Component = nullptr;
			}
			continue;
		}

		// 조건이 성립하는데 소리가 없으면 다시 시작한다
		if (!Component || !Component->IsPlaying())
		{
			UAudioComponent* Restarted = SpawnForEntry(
				*Entry, Active.Location, Active.AttachTo.Get(), Active.SocketName, Active.bForce2D);
			if (Restarted)
			{
				Restarted->FadeIn(FadeSecondsOnConditionChange, 1.0f);
				Active.Component = Restarted;
				if (MD_AudioPrivate::ShouldApplyDepthFilter(*Entry))
				{
					ApplyUnderwaterFilter(Restarted);
				}
				else if (MD_AudioPrivate::ShouldApplyDepthPitch(
					Entry->Space == EMD_AudioSpace::Underwater, Entry->bDepthPitch))
				{
					Restarted->SetPitchMultiplier(Entry->PitchMultiplier * CurrentDepthPitch);
				}
			}
		}
	}
}

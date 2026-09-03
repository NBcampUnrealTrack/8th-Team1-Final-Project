#include "Audio/MD_AudioSubsystem.h"

#include "Data/Audio/MD_AudioCategoryData.h"
#include "Data/Audio/MD_AudioSettings.h"
#include "Gameplay/Settings/MD_AudioSettingCategory.h"
#include "Gameplay/Settings/MD_SettingsManager.h"

#include "UI/Loading/MD_LoadingScreenSubsystem.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Sound/SoundBase.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
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

	bool ShouldApplyDepthPitch(bool bUnderwater, bool bDepthPitch, const FGameplayTag& EventTag)
	{
		if (!bUnderwater)
		{
			return false;
		}

		if (bDepthPitch)
		{
			return true;
		}

		const FGameplayTag AmbienceRoot = GetAmbienceRootTag();
		return AmbienceRoot.IsValid() && EventTag.MatchesTag(AmbienceRoot);
	}

	bool ShouldApplyDepthPitch(const FMD_AudioEntry& Entry)
	{
		return ShouldApplyDepthPitch(
			Entry.Space == EMD_AudioSpace::Underwater, Entry.bDepthPitch, Entry.EventTag);
	}

	float GetUserVolume(FGameplayTag EventTag, float MusicVolume, float EffectsVolume)
	{
		// 배경 음악 역할의 앰비언스 최상위 태그 조회
		FGameplayTag AmbienceRoot = GetAmbienceRootTag();
		// 기본 효과음 배율을 기준으로 한 결과값
		float UserVolume = EffectsVolume;
		// 앰비언스 항목의 배경 음악 음량 반환
		bool bHasAmbienceRoot = AmbienceRoot.IsValid();
		if (bHasAmbienceRoot == true)
		{
			// 현재 이벤트의 앰비언스 계층 포함 여부
			bool bIsAmbience = EventTag.MatchesTag(AmbienceRoot);
			if (bIsAmbience == true)
			{
				UserVolume = MusicVolume;
			}
		}

		// 판별된 사용자 카테고리 음량 반환
		return UserVolume;
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

	// 게임 인스턴스에 복원된 사용자 음량 조회
	UMD_SettingsManager* SettingsManager = UMD_SettingsManager::Get(this);
	if (IsValid(SettingsManager) == true)
	{
		// 현재 오디오 설정 인스턴스 조회
		UMD_AudioSettingCategory* AudioCategory = SettingsManager->GetAudioSettings();
		if (IsValid(AudioCategory) == true)
		{
			// 새 월드에서 재생될 소리에 저장된 카테고리별 배율 적용
			ApplyUserVolumes(AudioCategory->Settings.MasterVolume, AudioCategory->Settings.MusicVolume,
				AudioCategory->Settings.EffectsVolume);
		}
	}

	if (!CachedFilter)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MD_Audio] 수중 필터 프리셋이 설정되지 않았습니다. Project Settings → Game → MD Audio 에서 지정하세요."));
	}
}

void UMD_AudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const UMD_AudioSettings& Settings = UMD_AudioSettings::Get();

	// PIE 는 맵 이름에 UEDPIE_0_ 같은 접두사를 붙인다.
	// 붙은 채로 비교하면 패키지에서만 울리고 에디터에서는 조용해진다
	FString CurrentMap = InWorld.GetMapName();
	CurrentMap.RemoveFromStart(InWorld.StreamingLevelsPrefix);

	for (const FMD_AudioAutoPlayEntry& Candidate : Settings.AutoPlayOnWorldReady)
	{
		if (!Candidate.EventTag.IsValid())
		{
			continue;
		}

		const FString TargetMap = Candidate.Map.GetAssetName();
		if (!TargetMap.IsEmpty() && TargetMap != CurrentMap)
		{
			continue;
		}

		PendingAutoPlay = Candidate;
		AutoPlayDelayRemaining = Candidate.StartDelaySeconds;

		UE_LOG(LogTemp, Log, TEXT("[MD_Audio] '%s' 자동 재생 예약: %s"),
			*CurrentMap, *Candidate.EventTag.ToString());
		break;
	}
}

void UMD_AudioSubsystem::Deinitialize()
{
	// 월드 종료 시 사용자 음량 믹스 등록 해제
	if (bUserVolumeMixPushed == true)
	{
		UGameplayStatics::PopSoundMixModifier(this, UserVolumeMix);
		bUserVolumeMixPushed = false;
	}
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

	// 월드가 내려가는데 로드가 계속 돌 이유가 없다
	if (PreloadHandle.IsValid())
	{
		PreloadHandle->CancelHandle();
		PreloadHandle.Reset();
	}

	Super::Deinitialize();
}

void UMD_AudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 맵 로딩 중 초기화될 수 있는 오디오 믹스의 중복 등록 제거
	if (bUserVolumeMixPushed == true)
	{
		UGameplayStatics::PopSoundMixModifier(this, UserVolumeMix);
		bUserVolumeMixPushed = false;
	}

	// 월드 서브시스템 생성 이후 연결된 게임 인스턴스의 사용자 설정 조회
	UMD_SettingsManager* SettingsManager = UMD_SettingsManager::Get(this);
	if (IsValid(SettingsManager) == true)
	{
		// PIE 월드 생성 순서와 무관한 오디오 설정 복원
		UMD_AudioSettingCategory* AudioCategory = SettingsManager->GetAudioSettings();
		if (IsValid(AudioCategory) == true)
		{
			// 초기화 중 얻지 못한 전체 및 배경 및 효과음 음량 보완
			CurrentMasterVolume = AudioCategory->Settings.MasterVolume;
			CurrentMusicVolume = AudioCategory->Settings.MusicVolume;
			CurrentEffectsVolume = AudioCategory->Settings.EffectsVolume;
			// 새 월드의 실제 출력 장치와 온라인 입력 캡처에 확정된 선택 복원
			AudioCategory->RefreshAudioDevices();
		}
	}

	// 월드 오디오 장치 준비 후 최근 사용자 음량 재적용
	ApplyUserVolumes(CurrentMasterVolume, CurrentMusicVolume, CurrentEffectsVolume);
}

TStatId UMD_AudioSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMD_AudioSubsystem, STATGROUP_Tickables);
}

void UMD_AudioSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateAutoPlay(DeltaTime);
	UpdateUnderwaterCutoff(DeltaTime);
	EnforceLoopConditions();
	PruneFinished();
	DrawDebugInfo();
}

// ----------------------------------------------------------------- 자동 재생

bool UMD_AudioSubsystem::IsLoadingScreenBlocking() const
{
	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (!GameInstance)
	{
		return false;
	}

	const UMD_LoadingScreenSubsystem* LoadingScreen =
		GameInstance->GetSubsystem<UMD_LoadingScreenSubsystem>();

	return LoadingScreen && LoadingScreen->IsLoadingScreenActive();
}

void UMD_AudioSubsystem::UpdateAutoPlay(float DeltaTime)
{
	if (!PendingAutoPlay.IsSet())
	{
		return;
	}

	// 로딩 화면이 아직 덮고 있으면 지연 시간도 흐르지 않는다.
	// 화면이 걷히는 시점 기준으로 지연을 세야 매번 같은 타이밍이 나온다
	if (PendingAutoPlay->bWaitForLoadingScreen && IsLoadingScreenBlocking())
	{
		return;
	}

	AutoPlayDelayRemaining -= DeltaTime;
	if (AutoPlayDelayRemaining > 0.0f)
	{
		return;
	}

	// 한 월드에 한 번만 건다. 재생 실패든 성공이든 다시 시도하지 않는다
	const FGameplayTag EventTag = PendingAutoPlay->EventTag;
	PendingAutoPlay.Reset();

	if (!PlayEvent2D(EventTag).IsValid())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MD_Audio] 자동 재생 %s 에서 핸들이 돌아오지 않았습니다. ")
			TEXT("카탈로그에 항목이 있는지, bLooping 이 켜져 있는지 확인하세요."),
			*EventTag.ToString());
	}
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

	// 소재를 백그라운드로 올린다. 재생 순간에 로드가 걸리면 그 한 번이 굼뜨게 들린다
	if (PreloadHandle.IsValid())
	{
		PreloadHandle->CancelHandle();
		PreloadHandle.Reset();
	}

	TArray<FSoftObjectPath> SoundPaths;
	SoundPaths.Reserve(AllEntries.Num());
	for (const FMD_AudioEntry& Entry : AllEntries)
	{
		const FSoftObjectPath& Path = Entry.Sound.ToSoftObjectPath();
		if (Path.IsValid())
		{
			SoundPaths.AddUnique(Path);
		}
	}

	const int32 PreloadCount = SoundPaths.Num();
	if (PreloadCount > 0)
	{
		PreloadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
			MoveTemp(SoundPaths),
			FStreamableDelegate::CreateWeakLambda(this, [this, PreloadCount]()
			{
				UE_LOG(LogTemp, Log, TEXT("[MD_Audio] 소재 %d개 선로드 완료"), PreloadCount);
			}),
			FStreamableManager::DefaultAsyncLoadPriority,
			/*bManageActiveHandle*/ false,
			/*bStartStalled*/ false,
			TEXT("MD_Audio Preload"));
	}

	bIndexBuilt = true;
	UE_LOG(LogTemp, Log, TEXT("[MD_Audio] 이벤트 %d개 / 정의 %d건 인덱싱, 소재 %d개 선로드 요청"),
		EntriesByTag.Num(), AllEntries.Num(), PreloadCount);
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

	return RegisterPendingLoop(EventTag, Location, nullptr, NAME_None, false);
}

FMD_AudioHandle UMD_AudioSubsystem::PlayEventAttached(const FGameplayTag& EventTag, USceneComponent* AttachTo, FName SocketName)
{
	const FVector AttachLocation = AttachTo ? AttachTo->GetComponentLocation() : FVector::ZeroVector;

	if (const FMD_AudioEntry* Entry = ResolveEntry(EventTag))
	{
		return PlayResolved(*Entry, AttachLocation, AttachTo, SocketName, false);
	}

	return RegisterPendingLoop(EventTag, AttachLocation, AttachTo, SocketName, false);
}

FMD_AudioHandle UMD_AudioSubsystem::PlayEvent2D(const FGameplayTag& EventTag)
{
	if (const FMD_AudioEntry* Entry = ResolveEntry(EventTag))
	{
		return PlayResolved(*Entry, FVector::ZeroVector, nullptr, NAME_None, true);
	}

	return RegisterPendingLoop(EventTag, FVector::ZeroVector, nullptr, NAME_None, true);
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
	// 태그 카테고리에 적용된 사용자 음량 조회
	float UserVolume = MD_AudioPrivate::GetUserVolume(Entry.EventTag, CurrentMusicVolume, CurrentEffectsVolume);
	// 원본 항목 음량에 사용자 배율을 결합한 재생 음량
	float PlaybackVolume = Entry.VolumeMultiplier * UserVolume;

	if (bPlay2D)
	{
		// 보이스가 시작된 뒤에 필터를 켜면 반영되지 않는 경우가 있다.
		// 컴포넌트를 먼저 만들고 필터를 설정한 다음 재생한다.
		Component = UGameplayStatics::CreateSound2D(
			World, Sound, PlaybackVolume, Entry.PitchMultiplier,
			0.0f, Concurrency, /*bPersistAcrossLevelTransition*/ false, /*bAutoDestroy*/ !Entry.bLooping);

		if (Component && MD_AudioPrivate::ShouldApplyDepthFilter(Entry))
		{
			Component->bEnableLowPassFilter = true;
			Component->LowPassFilterFrequency = CurrentCutoffHz;
		}
		else if (Component)
		{
			ClearUnderwaterFilter(Component);
			if (MD_AudioPrivate::ShouldApplyDepthPitch(Entry))
			{
				Component->SetPitchMultiplier(Entry.PitchMultiplier * CurrentDepthPitch);
			}
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
			/*bStopWhenAttachedToDestroyed*/ true, PlaybackVolume, Entry.PitchMultiplier,
			0.0f, Attenuation, Concurrency, /*bAutoDestroy*/ !Entry.bLooping);
	}
	else
	{
		Component = UGameplayStatics::SpawnSoundAtLocation(
			World, Sound, Location, FRotator::ZeroRotator,
			PlaybackVolume, Entry.PitchMultiplier, 0.0f,
			Attenuation, Concurrency, /*bAutoDestroy*/ !Entry.bLooping);
	}

	if (MD_AudioPrivate::ShouldApplyDepthFilter(Entry))
	{
		ApplyUnderwaterFilter(Component);
	}
	else if (Component)
	{
		ClearUnderwaterFilter(Component);
		if (MD_AudioPrivate::ShouldApplyDepthPitch(Entry))
		{
			Component->SetPitchMultiplier(Entry.PitchMultiplier * CurrentDepthPitch);
		}
	}

	// 앞선 소리와 겹쳐 들려야 하는 항목은 짧게 페이드 인해 어택 충돌을 피한다
	if (Component && Entry.FadeInSeconds > 0.0f)
	{
		// 항목 기본 음량과 사용자 카테고리 음량을 보존한 페이드 적용
		Component->FadeIn(Entry.FadeInSeconds, PlaybackVolume);
	}

	return Component;
}

FMD_AudioHandle UMD_AudioSubsystem::PlayResolved(const FMD_AudioEntry& Entry, const FVector& Location,
	USceneComponent* AttachTo, FName SocketName, bool bForce2D)
{
	UAudioComponent* Component = SpawnForEntry(Entry, Location, AttachTo, SocketName, bForce2D);

	// 루프 항목에만 외부 정지용 핸들 발급
	FMD_AudioHandle Handle;
	if (Entry.bLooping == true)
	{
		Handle.Id = NextHandleId++;
	}

	// 재생 중 카테고리 음량 변경을 위한 컴포넌트 정보 구성
	FActiveSound Active;
	Active.Id = Handle.Id;
	Active.EventTag = Entry.EventTag;
	Active.Component = Component;
	Active.bAutoManage = false;
	// 루프 항목에서만 조건 기반 자동 관리 설정 반영
	if (Entry.bLooping == true)
	{
		Active.bAutoManage = Entry.bStopWhenConditionsFail;
	}
	Active.bUnderwater = (Entry.Space == EMD_AudioSpace::Underwater);
	Active.bDepthPitch = Entry.bDepthPitch;
	Active.BasePitch = Entry.PitchMultiplier;
	// 실행 중 사용자 음량 변경을 위한 원본 항목 음량 보관
	Active.BaseVolume = Entry.VolumeMultiplier;
	Active.FadeOutSeconds = Entry.FadeOutSeconds;
	Active.AttachTo = AttachTo;
	Active.SocketName = SocketName;
	Active.Location = Location;
	Active.bForce2D = bForce2D;
	ActiveSounds.Add(Active);

	// 루프 여부에 따른 유효 핸들 또는 기본 핸들 반환
	return Handle;
}

FMD_AudioHandle UMD_AudioSubsystem::RegisterPendingLoop(const FGameplayTag& EventTag, const FVector& Location,
	USceneComponent* AttachTo, FName SocketName, bool bForce2D)
{
	const TArray<FMD_AudioEntry>* Candidates = EntriesByTag.Find(EventTag);
	if (!Candidates)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[MD_Audio] 정의 없음: %s"), *EventTag.ToString());
		return FMD_AudioHandle();
	}

	// 자동관리 루프만 대기 등록한다. 원샷은 지금 조건이 안 맞으면 그냥 버린다
	const FMD_AudioEntry* Managed = nullptr;
	for (const FMD_AudioEntry& Candidate : *Candidates)
	{
		if (Candidate.bLooping && Candidate.bStopWhenConditionsFail)
		{
			Managed = &Candidate;
			break;
		}
	}

	if (!Managed)
	{
		return FMD_AudioHandle();
	}

	FMD_AudioHandle Handle;
	Handle.Id = NextHandleId++;

	FActiveSound Active;
	Active.Id = Handle.Id;
	Active.EventTag = EventTag;
	Active.Component = nullptr;      // 조건이 맞으면 EnforceLoopConditions 가 띄운다
	Active.bAutoManage = true;
	Active.bUnderwater = (Managed->Space == EMD_AudioSpace::Underwater);
	Active.bDepthPitch = Managed->bDepthPitch;
	Active.BasePitch = Managed->PitchMultiplier;
	Active.AttachTo = AttachTo;
	Active.SocketName = SocketName;
	Active.Location = Location;
	Active.bForce2D = bForce2D;
	ActiveSounds.Add(Active);

	UE_LOG(LogTemp, Verbose, TEXT("[MD_Audio] 조건 대기 등록: %s"), *EventTag.ToString());
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

		const float Fade = FadeOutSeconds < 0.0f
			? ActiveSounds[Index].FadeOutSeconds
			: FadeOutSeconds;

		if (UAudioComponent* Component = ActiveSounds[Index].Component.Get())
		{
			if (Fade > 0.0f)
			{
				Component->FadeOut(Fade, 0.0f);
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

void UMD_AudioSubsystem::ApplyUserVolumes(float MasterVolume, float MusicVolume, float EffectsVolume)
{
	// 사용자 음량의 정규화 범위 보정
	CurrentMasterVolume = FMath::Clamp(MasterVolume, 0.0f, 1.0f);
	CurrentMusicVolume = FMath::Clamp(MusicVolume, 0.0f, 1.0f);
	CurrentEffectsVolume = FMath::Clamp(EffectsVolume, 0.0f, 1.0f);

	// 에셋 변경 없는 사용자 전체 음량 전용 믹스 생성
	if (IsValid(UserVolumeMix) == false)
	{
		UserVolumeMix = NewObject<USoundMix>(this, NAME_None, RF_Transient);
		// 사용자가 적용한 전체 음량의 즉시 반영과 명시적 해제까지 유지
		UserVolumeMix->FadeInTime = 0.0f;
		UserVolumeMix->FadeOutTime = 0.0f;
		UserVolumeMix->Duration = -1.0f;
		UserVolumeMix->bApplyEQ = false;
	}

	// 기본 사운드 클래스와 자식 계층의 전체 음량 적용
	const UAudioSettings* EngineAudioSettings = GetDefault<UAudioSettings>();
	USoundClass* MasterSoundClass = EngineAudioSettings->GetDefaultSoundClass();
	if (IsValid(MasterSoundClass) == true)
	{
		UGameplayStatics::SetSoundMixClassOverride(this, UserVolumeMix, MasterSoundClass,
			CurrentMasterVolume, 1.0f, 0.0f, true);
		// 현재 월드의 오디오 장치 준비 여부 확인
		UWorld* World = GetWorld();
		if (IsValid(World) == true)
		{
			FAudioDevice* AudioDevice = World->GetAudioDeviceRaw();
			if (AudioDevice != nullptr)
			{
				// 반복 적용 시 믹스 참조 횟수가 누적되지 않는 단일 등록
				if (bUserVolumeMixPushed == false)
				{
					UGameplayStatics::PushSoundMixModifier(this, UserVolumeMix);
					bUserVolumeMixPushed = true;
				}
			}
		}
	}

	// 이미 재생 중인 관리 대상의 카테고리별 음량 즉시 갱신
	for (FActiveSound& Active : ActiveSounds)
	{
		// 현재 관리 대상의 오디오 컴포넌트 조회
		UAudioComponent* Component = Active.Component.Get();
		if (IsValid(Component) == false)
		{
			continue;
		}

		// 원본 항목 음량과 사용자 카테고리 배율의 결합
		float UserVolume = MD_AudioPrivate::GetUserVolume(
			Active.EventTag, CurrentMusicVolume, CurrentEffectsVolume);
		Component->SetVolumeMultiplier(Active.BaseVolume * UserVolume);
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

void UMD_AudioSubsystem::ClearUnderwaterFilter(UAudioComponent* Component) const
{
	if (!Component)
	{
		return;
	}

	Component->SetLowPassFilterEnabled(false);
}

void UMD_AudioSubsystem::ApplyCutoffToActiveSounds()
{
	LastAppliedCount = 0;
	LastLpfAppliedCount = 0;
	LastPitchAppliedCount = 0;
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
			++LastLpfAppliedCount;
			continue;
		}

		ClearUnderwaterFilter(Component);

		if (MD_AudioPrivate::ShouldApplyDepthPitch(
			Active.bUnderwater, Active.bDepthPitch, Active.EventTag))
		{
			Component->SetPitchMultiplier(Active.BasePitch * CurrentDepthPitch);
			++LastAppliedCount;
			++LastPitchAppliedCount;
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
	Line(4, FString::Printf(TEXT("  cutoff (SFX)  : %.0f Hz  [%.0f -> %.0f]  (ambience LPF off)"),
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
	Line(9, FString::Printf(TEXT("  depth fx      : lpf %d  pitch %d  skipped %d"),
		LastLpfAppliedCount, LastPitchAppliedCount, LastSkippedCount));
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
				// 조건 복구 후에도 현재 사용자 카테고리 음량을 유지하는 페이드 목표값
				float UserVolume = MD_AudioPrivate::GetUserVolume(Entry->EventTag, CurrentMusicVolume, CurrentEffectsVolume);
				float TargetVolume = Entry->VolumeMultiplier * UserVolume;
				Restarted->FadeIn(FadeSecondsOnConditionChange, TargetVolume);
				Active.Component = Restarted;
				// 조건에 따라 선택된 최신 항목의 원본 음량 보관
				Active.BaseVolume = Entry->VolumeMultiplier;
				if (MD_AudioPrivate::ShouldApplyDepthFilter(*Entry))
				{
					ApplyUnderwaterFilter(Restarted);
				}
				else
				{
					ClearUnderwaterFilter(Restarted);
					if (MD_AudioPrivate::ShouldApplyDepthPitch(*Entry))
					{
						Restarted->SetPitchMultiplier(Entry->PitchMultiplier * CurrentDepthPitch);
					}
				}
			}
		}
	}
}

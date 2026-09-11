#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "Misc/Optional.h"
#include "Data/Audio/MD_AudioTypes.h"
#include "MD_AudioSubsystem.generated.h"

class UAudioComponent;
class USoundBase;
class USoundMix;
class USubmixEffectFilterPreset;

/** 루프 사운드를 나중에 멈추기 위한 핸들 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_AudioHandle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MD|Audio")
	int32 Id = 0;

	bool IsValid() const { return Id > 0; }
};

/**
 * 효과음 재생 진입점.
 *
 * 게임플레이 코드는 사운드 에셋을 직접 들고 있지 않고 **이벤트 태그만** 넘긴다.
 * 어떤 소재가 나갈지는 데이터 에셋과 현재 상태·환경 태그가 결정한다.
 *
 * 수중 LPF 컷오프 구동도 여기서 한다. 플레이어 카메라 깊이를 읽어
 * `SEF_MD_UnderwaterLPF` 의 컷오프를 2kHz ~ 800Hz 사이로 움직인다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_AudioSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	//~ UWorldSubsystem
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	//~ FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** 월드 위치에서 1회 재생. 루프 항목이면 핸들이 유효하게 돌아온다 */
	UFUNCTION(BlueprintCallable, Category = "MD|Audio", meta = (AutoCreateRefTerm = "EventTag"))
	FMD_AudioHandle PlayEventAtLocation(const FGameplayTag& EventTag, const FVector& Location);

	/** 액터에 붙여 재생. 대상이 움직이는 소리(수영·끌기·분출)에 쓴다 */
	UFUNCTION(BlueprintCallable, Category = "MD|Audio", meta = (AutoCreateRefTerm = "EventTag"))
	FMD_AudioHandle PlayEventAttached(const FGameplayTag& EventTag, USceneComponent* AttachTo, FName SocketName = NAME_None);

	/** 위치 없이 2D 재생. UI·정산·경고음 */
	UFUNCTION(BlueprintCallable, Category = "MD|Audio", meta = (AutoCreateRefTerm = "EventTag"))
	FMD_AudioHandle PlayEvent2D(const FGameplayTag& EventTag);

	/** 루프 정지. FadeOutSeconds 0 이면 즉시 */
	UFUNCTION(BlueprintCallable, Category = "MD|Audio")
	void StopHandle(const FMD_AudioHandle& Handle, float FadeOutSeconds = 0.1f);

	/** 해당 이벤트 태그로 재생 중인 모든 루프를 정지 */
	UFUNCTION(BlueprintCallable, Category = "MD|Audio", meta = (AutoCreateRefTerm = "EventTag"))
	void StopEvent(const FGameplayTag& EventTag, float FadeOutSeconds = 0.1f);

	/** 상태 태그 갱신 — 부상·산소저하 같은 캐릭터 상태. 항목 선택에 쓰인다 */
	UFUNCTION(BlueprintCallable, Category = "MD|Audio|상태")
	void SetStateTag(const FGameplayTag& StateTag, bool bActive);

	/** 환경 태그 갱신 — 암전·모선·섹터 같은 공간 상태 */
	UFUNCTION(BlueprintCallable, Category = "MD|Audio|상태")
	void SetEnvironmentTag(const FGameplayTag& EnvironmentTag, bool bActive);

	UFUNCTION(BlueprintPure, Category = "MD|Audio|상태")
	const FGameplayTagContainer& GetStateTags() const { return ActiveStateTags; }

	UFUNCTION(BlueprintPure, Category = "MD|Audio|상태")
	const FGameplayTagContainer& GetEnvironmentTags() const { return ActiveEnvironmentTags; }

	/** 현재 적용 중인 수중 LPF 컷오프(Hz). 디버그 표시용 */
	UFUNCTION(BlueprintPure, Category = "MD|Audio|수중")
	float GetCurrentCutoffHz() const { return CurrentCutoffHz; }

	/** 카탈로그를 다시 읽어 태그 인덱스를 재구성한다. 데이터 에셋 편집 후 호출 */
	UFUNCTION(BlueprintCallable, Category = "MD|Audio")
	void RebuildIndex();

	/** 사용자 설정의 전체 및 배경 음악 및 효과음 배율 적용 */
	UFUNCTION(BlueprintCallable, Category = "MD|Audio")
	void ApplyUserVolumes(float MasterVolume, float MusicVolume, float EffectsVolume);

protected:
	// 월드 시작 시 사용자 음량과 입출력 장치 복원
	void RestoreWorldAudioSettings();

	// 현재 맵에 대응하는 자동 재생 항목 예약
	void ScheduleWorldAutoPlay(UWorld& InWorld);

private:
	/** 상태·환경 조건을 만족하는 항목 중 가장 구체적인 것을 고른다 */
	const FMD_AudioEntry* ResolveEntry(const FGameplayTag& EventTag) const;

	/** 현재 상태·환경 태그로 이 항목을 쓸 수 있는지 */
	bool PassesConditions(const FMD_AudioEntry& Entry) const;

	/** 조건이 깨진 루프를 멈춘다 (bStopWhenConditionsFail) */
	void EnforceLoopConditions();

	/** md.Audio.Debug 1 일 때 화면에 상태를 찍는다 */
	void DrawDebugInfo();

	/** 등록된 자동 재생 항목의 대기 조건을 보고, 만족하면 시작한다 */
	void UpdateAutoPlay(float DeltaTime);

	/** 로딩 화면이 아직 화면을 덮고 있는지. outro 는 덮은 것으로 보지 않는다 */
	bool IsLoadingScreenBlocking() const;

	/** 살아 있는 수중 사운드에 현재 컷오프를 적용한다 */
	void ApplyCutoffToActiveSounds();

	/** 재생 직후 컴포넌트에 수중 로우패스를 걸어 준다 */
	void ApplyUnderwaterFilter(UAudioComponent* Component) const;

	/** 앰비언스 등 LPF를 쓰지 않는 항목에서 컴포넌트 로우패스를 끈다 */
	void ClearUnderwaterFilter(UAudioComponent* Component) const;

	FMD_AudioHandle PlayResolved(const FMD_AudioEntry& Entry, const FVector& Location,
		USceneComponent* AttachTo, FName SocketName, bool bForce2D);

	/** 컴포넌트만 띄운다. 핸들 관리는 호출자가 한다 */
	UAudioComponent* SpawnForEntry(const FMD_AudioEntry& Entry, const FVector& Location,
		USceneComponent* AttachTo, FName SocketName, bool bForce2D);

	/**
	 * 지금은 조건이 안 맞아 재생할 수 없지만, 조건이 돌아오면 시작해야 하는
	 * 자동관리 루프를 요청만 등록한다. (수면 위 앰비를 물속에서 요청한 경우 등)
	 */
	FMD_AudioHandle RegisterPendingLoop(const FGameplayTag& EventTag, const FVector& Location,
		USceneComponent* AttachTo, FName SocketName, bool bForce2D);

	void UpdateUnderwaterCutoff(float DeltaTime);

	/** 캡슐 잠김으로 Env.Surface.Above 를 갱신한다 */
	void UpdateSurfaceTag(const class APlayerController* PC);

	/** 끝난 원샷 컴포넌트를 정리한다 */
	void PruneFinished();

	/**
	 * 인덱스에 들어온 소재를 백그라운드로 미리 올린다.
	 *
	 * 재생 시점의 LoadSynchronous 는 첫 타에만 걸리지만, 그 첫 타가 하필
	 * 플레이어가 처음 긁는 순간이라 "반응이 굼뜨다" 로 체감된다.
	 * 동기로 올리면 그 비용이 레벨 로드로 옮겨갈 뿐이라 카탈로그가 커질수록
	 * 로딩이 길어진다. 핸들을 들고 있으면 GC 로 풀리는 것도 막힌다.
	 *
	 * 선로드가 끝나기 전에 재생 요청이 오면 그 건만 기존대로 동기 로드된다.
	 */
	TSharedPtr<struct FStreamableHandle> PreloadHandle;

	/** EventTag → 후보 항목들 */
	TMap<FGameplayTag, TArray<FMD_AudioEntry>> EntriesByTag;

	UPROPERTY(Transient)
	FGameplayTagContainer ActiveStateTags;

	UPROPERTY(Transient)
	FGameplayTagContainer ActiveEnvironmentTags;

	/**
	 * 살아 있는 재생 인스턴스.
	 *
	 * bAutoManage 인 항목은 조건이 깨지면 컴포넌트만 버리고 요청은 남겨 둔다.
	 * 조건이 다시 성립하면 같은 자리에서 재개한다 (물에 들어갔다 나오는 앰비언스).
	 */
	struct FActiveSound
	{
		int32 Id = 0;
		FGameplayTag EventTag;
		TWeakObjectPtr<UAudioComponent> Component;

		/** 조건 변화에 따라 스스로 멈추고 다시 시작한다 */
		bool bAutoManage = false;

		/** 수중 공간 항목. 컷오프를 매 갱신마다 이 컴포넌트에 적용한다 */
		bool bUnderwater = false;

		/** 수심에 따라 피치를 내린다 */
		bool bDepthPitch = false;

		/** 항목이 지정한 기본 피치. 깊이 배율을 여기에 곱한다 */
		float BasePitch = 1.0f;

		/** 항목 데이터에 지정된 사용자 설정 적용 전 기본 음량 */
		float BaseVolume = 1.0f;

		/** 항목이 지정한 기본 정지 페이드. StopHandle 에 음수를 넘기면 이 값을 쓴다 */
		float FadeOutSeconds = 0.1f;

		// 재개에 필요한 재생 위치 정보
		TWeakObjectPtr<USceneComponent> AttachTo;
		FName SocketName = NAME_None;
		FVector Location = FVector::ZeroVector;
		bool bForce2D = false;
	};
	TArray<FActiveSound> ActiveSounds;
	int32 NextHandleId = 1;

	UPROPERTY(Transient)
	TObjectPtr<USubmixEffectFilterPreset> CachedFilter;

	/**
	 * 이 월드에서 아직 시작하지 못한 자동 재생 항목. 시작하면 비운다.
	 * 월드 서브시스템이라 맵을 바꾸면 자연히 새로 뽑힌다.
	 */
	TOptional<FMD_AudioAutoPlayEntry> PendingAutoPlay;

	/** 대기 조건이 풀린 뒤 남은 지연(초) */
	float AutoPlayDelayRemaining = 0.0f;

	// 카메라 페이드와 독립적인 사용자 전체 음량용 런타임 믹스
	UPROPERTY(Transient)
	TObjectPtr<USoundMix> UserVolumeMix;

	// 런타임 믹스 중복 등록 방지 상태
	bool bUserVolumeMixPushed = false;

	float CurrentCutoffHz = 2000.0f;
	float CurrentDepthPitch = 1.0f;
	// 새 재생과 월드 시작 시 사용하는 최근 적용 전체 음량
	float CurrentMasterVolume = 1.0f;
	// 새 배경 재생에 사용하는 최근 적용 음량
	float CurrentMusicVolume = 1.0f;
	// 새 효과음 재생에 사용하는 최근 적용 음량
	float CurrentEffectsVolume = 1.0f;
	int32 LastAppliedCount = 0;
	int32 LastLpfAppliedCount = 0;
	int32 LastPitchAppliedCount = 0;
	int32 LastSkippedCount = 0;
	float CutoffUpdateAccumulator = 0.0f;
	bool bIndexBuilt = false;
};

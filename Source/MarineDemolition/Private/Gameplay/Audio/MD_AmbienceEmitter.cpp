#include "Gameplay/Audio/MD_AmbienceEmitter.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"

AMD_AmbienceEmitter::AMD_AmbienceEmitter()
{
	PrimaryActorTick.bCanEverTick = false;

	// 배치 기준점. 3D 앰비는 이 위치에서 재생된다
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

UMD_AudioSubsystem* AMD_AmbienceEmitter::GetAudioSubsystem() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UMD_AudioSubsystem>() : nullptr;
}

void AMD_AmbienceEmitter::BeginPlay()
{
	Super::BeginPlay();

	if (UMD_AudioSubsystem* Audio = GetAudioSubsystem())
	{
		if (ApplyEnvironmentTagOnBeginPlay.IsValid())
		{
			Audio->SetEnvironmentTag(ApplyEnvironmentTagOnBeginPlay, true);
		}
	}

	if (bAutoPlay)
	{
		StartAmbience();
	}
}

void AMD_AmbienceEmitter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopAmbience();

	if (UMD_AudioSubsystem* Audio = GetAudioSubsystem())
	{
		if (ApplyEnvironmentTagOnBeginPlay.IsValid())
		{
			Audio->SetEnvironmentTag(ApplyEnvironmentTagOnBeginPlay, false);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AMD_AmbienceEmitter::StartAmbience()
{
	if (Handle.IsValid())
	{
		return;
	}

	UMD_AudioSubsystem* Audio = GetAudioSubsystem();
	if (!Audio || !AmbienceTag.IsValid())
	{
		return;
	}

	// 루트에 붙여 재생한다. 항목이 2D 면 서브시스템이 위치를 무시한다
	Handle = Audio->PlayEventAttached(AmbienceTag, GetRootComponent());
}

void AMD_AmbienceEmitter::StopAmbience()
{
	if (!Handle.IsValid())
	{
		return;
	}

	if (UMD_AudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->StopHandle(Handle, FadeOutSeconds);
	}

	Handle = FMD_AudioHandle();
}

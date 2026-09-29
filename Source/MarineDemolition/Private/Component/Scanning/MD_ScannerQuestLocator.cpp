#include "Component/Scanning/MD_ScannerComponent.h"

#include "Character/MD_Character.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "World/Sample/MD_QuestItem.h"

void UMD_ScannerComponent::StopQuestLocator()
{
	if (IsValid(QuestLocatorAudio))
	{
		QuestLocatorAudio->Stop();
		QuestLocatorAudio->DestroyComponent();
	}
	QuestLocatorAudio = nullptr;
	QuestLocatorTarget.Reset();
	NextQuestLocatorTime = 0;
	QuestLocatorIndex = 0;
}

void UMD_ScannerComponent::UpdateQuestLocator()
{
	const AMD_Character* Character = Cast<AMD_Character>(GetOwner());
	const APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	UWorld* World = GetWorld();
	const UMD_InteractionComponent* Interaction = Character ? Character->GetInteractionComponent() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer || !PC || !PC->IsLocalController()
		|| !bEnableQuestLocator || !IsScannerEquipped() || !QuestLocatorSound
		|| !FMath::IsFinite(QuestLocatorRadius) || QuestLocatorRadius <= 0
		|| UGameplayStatics::IsGamePaused(this) || PC->IsMoveInputIgnored() || PC->IsLookInputIgnored()
		|| (Interaction && Interaction->GetHeldActor()))
	{
		StopQuestLocator();
		return;
	}

	TSet<const AActor*> HeldItems;
	for (TActorIterator<APawn> It(World); It; ++It)
		if (const auto* OtherInteraction = It->FindComponentByClass<UMD_InteractionComponent>())
			if (AActor* Held = OtherInteraction->GetHeldActor()) HeldItems.Add(Held);

	const FVector Origin = Character->GetActorLocation();
	const double RadiusSquared = FMath::Square(static_cast<double>(QuestLocatorRadius));
	TArray<AMD_QuestItem*> Targets;
	for (TActorIterator<AMD_QuestItem> It(World); It; ++It)
	{
		AMD_QuestItem* Item = *It;
		if (!IsValid(Item) || !Item->QuestDefinitionId.IsValid() || Item->IsHidden()
			|| Item->GetAttachParentActor() || HeldItems.Contains(Item)
			|| FVector::DistSquared(Origin, Item->GetActorLocation()) > RadiusSquared) continue;
		Targets.Add(Item);
	}
	if (Targets.IsEmpty())
	{
		StopQuestLocator();
		return;
	}
	// Stop an in-flight beep as soon as its sample is carried, hidden, destroyed or out of range.
	if (QuestLocatorAudio && !Targets.Contains(QuestLocatorTarget.Get()))
	{
		QuestLocatorAudio->Stop();
		QuestLocatorAudio->DestroyComponent();
		QuestLocatorAudio = nullptr;
		QuestLocatorTarget.Reset();
	}
	if (World->GetTimeSeconds() < NextQuestLocatorTime) return;
	// Stable order avoids starving a target when distances change while swimming.
	Targets.Sort([](const AMD_QuestItem& A, const AMD_QuestItem& B) { return A.GetFName().LexicalLess(B.GetFName()); });
	AMD_QuestItem* Target = Targets[QuestLocatorIndex % Targets.Num()];
	QuestLocatorIndex = (QuestLocatorIndex + 1) % Targets.Num();
	if (IsValid(QuestLocatorAudio)) QuestLocatorAudio->DestroyComponent();
	QuestLocatorAudio = NewObject<UAudioComponent>(GetOwner());
	QuestLocatorAudio->bAutoActivate = false;
	QuestLocatorAudio->bAutoDestroy = false;
	QuestLocatorAudio->bStopWhenOwnerDestroyed = true;
	QuestLocatorAudio->SetupAttachment(Target->GetRootComponent());
	QuestLocatorAudio->SetRelativeLocation(FVector::ZeroVector);
	QuestLocatorAudio->RegisterComponent();
	FSoundAttenuationSettings Attenuation;
	Attenuation.bSpatialize = true;
	Attenuation.bAttenuate = true;
	Attenuation.bEnableOcclusion = false;
	Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
	Attenuation.AttenuationShapeExtents = FVector(100.f, 0.f, 0.f);
	Attenuation.FalloffDistance = FMath::Max(100.f, QuestLocatorRadius - 100.f);
	QuestLocatorAudio->AdjustAttenuation(Attenuation);
	QuestLocatorAudio->SetSound(QuestLocatorSound);
	QuestLocatorAudio->Play();
	QuestLocatorTarget = Target;
	const float Interval = FMath::IsFinite(QuestLocatorInterval) ? FMath::Max(0.6f, QuestLocatorInterval) : 1.5f;
	NextQuestLocatorTime = World->GetTimeSeconds() + Interval;
}

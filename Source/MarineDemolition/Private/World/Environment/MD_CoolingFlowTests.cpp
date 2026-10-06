#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "AbilitySystemComponent.h"
#include "Character/MD_Character.h"
#include "Components/BoxComponent.h"
#include "Gameplay/Attribute/MD_CharacterAttributeSet.h"
#include "Player/MD_PlayerState.h"
#include "Component/Hazard/MD_HazardVolumeComponent.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "World/Environment/MD_CoolingHazardZone.h"
#include "World/Wall/MD_CoolingPump.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "NiagaraComponent.h"

namespace
{
/** Tick on separate engine frames; a synchronous loop cannot exercise frame-guarded timers. */
class FMDCoolingTimedDamageCheck final : public IAutomationLatentCommand
{
public:
	FMDCoolingTimedDamageCheck(FAutomationTestBase* InTest, UWorld* InWorld,
		AMD_CoolingPump* InPump, AMD_CoolingHazardZone* InZone,
		AMD_Character* InCharacter, UAbilitySystemComponent* InASC)
		: Test(InTest), World(InWorld), Pump(InPump), Zone(InZone), Character(InCharacter), ASC(InASC)
	{
	}

	virtual ~FMDCoolingTimedDamageCheck() override
	{
		if (IsValid(World))
		{
			World->DestroyWorld(false);
			if (GEngine) GEngine->DestroyWorldContext(World);
		}
	}

	virtual bool Update() override
	{
		if (!IsValid(World) || !IsValid(Pump) || !IsValid(Zone) || !IsValid(Character) || !IsValid(ASC))
		{
			Test->AddError(TEXT("Cooling test fixture expired before timed checks completed"));
			return true;
		}
		World->Tick(LEVELTICK_All, 0.1f);
		++FramesInPhase;
		const float Health = ASC->GetNumericAttribute(UMD_CharacterAttributeSet::GetHealthAttribute());
		if (Phase == 0 && FramesInPhase >= 11)
		{
			Test->TestEqual(TEXT("Room continues 8 HP per second"), Health, 84.f);
			Character->SetActorLocation(FVector(12000.f, 0.f, 0.f));
			Test->TestFalse(TEXT("Character has left room"), Zone->DamageZone->IsOverlappingActor(Character));
			Phase = 1;
			FramesInPhase = 0;
		}
		else if (Phase == 1 && FramesInPhase >= 11)
		{
			Test->TestEqual(TEXT("Leaving room stops damage"), Health, 84.f);
			Character->SetActorLocation(FVector(10000.f, 0.f, 0.f));
			Test->TestEqual(TEXT("Re-entry after cooldown deals immediate damage"),
				ASC->GetNumericAttribute(UMD_CharacterAttributeSet::GetHealthAttribute()), 76.f);
			Test->TestTrue(TEXT("Foam repairs while room occupied"),
				Pump->TryExternalRepair(Character, MDGameplayTags::Ability_UtilityRepair_Foam));
			Test->TestFalse(TEXT("Occupied room immediately safe"), Zone->IsHazardActive());
			Phase = 2;
			FramesInPhase = 0;
		}
		else if (Phase == 2 && FramesInPhase >= 12)
		{
			Test->TestEqual(TEXT("Repair stops damage to pawn already inside"), Health, 76.f);
			Zone->CoolingPump.Reset();
			Test->TestTrue(TEXT("Missing reference fails closed"), Zone->IsHazardActive());
			Test->AddInfo(TEXT("Cooling timed checks finished: entry, periodic damage, exit, re-entry, repair"));
			return true;
		}
		return false;
	}

private:
	FAutomationTestBase* Test;
	UWorld* World;
	AMD_CoolingPump* Pump;
	AMD_CoolingHazardZone* Zone;
	AMD_Character* Character;
	UAbilitySystemComponent* ASC;
	int32 Phase = 0;
	int32 FramesInPhase = 0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDCoolingFlowTest, "MarineDemolition.Quest.Day3.CoolingPump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMDCoolingFlowTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues InitValues = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &InitValues);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	bool bDeferredCleanup = false;
	ON_SCOPE_EXIT
	{
		if (!bDeferredCleanup)
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	};
	AMD_CoolingPump* Pump = World->SpawnActor<AMD_CoolingPump>();
	AMD_CoolingHazardZone* Zone = World->SpawnActor<AMD_CoolingHazardZone>();
	if (!TestNotNull(TEXT("Pump"), Pump) || !TestNotNull(TEXT("Zone"), Zone))
	{
		return false;
	}
	Zone->CoolingPump = Pump;
	// Match real actor/component initialization so GAS attributes, overlaps and
	// dynamic damage delegates work without enabling editor-script bypasses.
	// A native, uninitialized game instance supplies the GameMode factory without
	// starting project persistence subsystems. Pawn damage still uses engine authority checks.
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	FURL TestURL;
	TestURL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
	if (!TestTrue(TEXT("Native test GameMode created"), World->SetGameMode(TestURL)))
	{
		return false;
	}
	World->InitializeActorsForPlay(TestURL);
	World->BeginPlay();
	TestTrue(TEXT("Test world initialized for play"), World->AreActorsInitialized());
	TestTrue(TEXT("Test world has begun play"), World->HasBegunPlay());
	UMD_HazardVolumeComponent* LocalHazard = Pump->FindComponentByClass<UMD_HazardVolumeComponent>();
	UNiagaraComponent* LeakFX = Pump->FindComponentByClass<UNiagaraComponent>();

	TestEqual(TEXT("Starts broken"), Pump->GetUtilityState(), EMD_UtilityState::Broken);
	TestTrue(TEXT("Room dangerous before repair"), Zone->IsHazardActive());
	TestNull(TEXT("Pump has no duplicate local damage volume"), LocalHazard);
	TestNotNull(TEXT("Leak Niagara component"), LeakFX);
	TestNotNull(TEXT("Leak Niagara asset"), LeakFX ? LeakFX->GetAsset() : nullptr);
	TestFalse(TEXT("No free E repair"), Pump->CanInteract_Implementation(nullptr));
	TestFalse(TEXT("Wrong tool rejected"), Pump->TryExternalRepair(nullptr, MDGameplayTags::Ability_UtilityRepair_Insulation));
	TestTrue(TEXT("Foam repairs pump"), Pump->TryExternalRepair(nullptr, MDGameplayTags::Ability_UtilityRepair_Foam));
	TestTrue(TEXT("Cooling restored"), Pump->IsCoolingRestored());
	TestFalse(TEXT("Room immediately safe"), Zone->IsHazardActive());
	TestFalse(TEXT("Duplicate repair rejected"), Pump->TryExternalRepair(nullptr, MDGameplayTags::Ability_UtilityRepair_Foam));
	// Exercise actual room overlap -> ApplyDamage -> player GAS Health.
	Zone->SetActorLocation(FVector(10000.f, 0.f, 0.f));
	AMD_PlayerState* PlayerState = World->SpawnActor<AMD_PlayerState>();
	AMD_Character* Character = World->SpawnActor<AMD_Character>(FVector(12000.f, 0.f, 0.f), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("Damage test character"), Character) && TestNotNull(TEXT("Damage test state"), PlayerState))
	{
		Character->SetPlayerState(PlayerState);
		// Keep the fixture stationary; this test exercises hazards, not locomotion.
		Character->GetCharacterMovement()->DisableMovement();
		TestTrue(TEXT("Damage test character began play"), Character->HasActorBegunPlay());
		UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
		UMD_CharacterAttributeSet* Attributes = PlayerState->GetCharacterAttributeSet();
		if (TestNotNull(TEXT("Character ASC"), ASC) && TestNotNull(TEXT("Character attributes"), Attributes))
		{
			TestTrue(TEXT("Native attributes registered during actor initialization"),
				ASC->GetSet<UMD_CharacterAttributeSet>() == Attributes);
			ASC->InitAbilityActorInfo(PlayerState, Character);
			ASC->SetNumericAttributeBase(UMD_CharacterAttributeSet::GetMaxHealthAttribute(), 100.f);
			ASC->SetNumericAttributeBase(UMD_CharacterAttributeSet::GetHealthAttribute(), 100.f);
			TestEqual(TEXT("Test character starts with 100 HP"),
				ASC->GetNumericAttribute(UMD_CharacterAttributeSet::GetHealthAttribute()), 100.f);
			TestTrue(TEXT("Character accepts engine damage"), Character->CanBeDamaged());
			TestTrue(TEXT("Character damage bridge is bound"), Character->OnTakeAnyDamage.IsBound());
			TestTrue(TEXT("Room overlap handler is bound"), Zone->DamageZone->OnComponentBeginOverlap.IsBound());
			TestEqual(TEXT("Engine accepts pawn damage"),
				UGameplayStatics::ApplyDamage(Character, 1.f, nullptr, Zone, nullptr), 1.f);
			TestEqual(TEXT("Native damage bridge changes HP"),
				ASC->GetNumericAttribute(UMD_CharacterAttributeSet::GetHealthAttribute()), 99.f);
			ASC->SetNumericAttributeBase(UMD_CharacterAttributeSet::GetHealthAttribute(), 100.f);
			// 저장 복원 경로가 없으므로 시작 파손 상태의 새 펌프로 진입 검사
			AMD_CoolingPump* EntryPump = World->SpawnActor<AMD_CoolingPump>();
			if (!TestNotNull(TEXT("Entry pump"), EntryPump)) return false;
			Zone->CoolingPump = EntryPump;
			TestEqual(TEXT("Entry pump starts broken"), EntryPump->GetUtilityState(), EMD_UtilityState::Broken);
			TestTrue(TEXT("Room is hazardous before entry"), Zone->IsHazardActive());
			Character->SetActorLocation(FVector(10000.f, 0.f, 0.f));
			TestTrue(TEXT("Character actually overlaps room"), Zone->DamageZone->IsOverlappingActor(Character));
			TestEqual(TEXT("Entry deals 8 HP immediately"),
				ASC->GetNumericAttribute(UMD_CharacterAttributeSet::GetHealthAttribute()), 92.f);
			// A second component overlap in the same instant must not multiply damage.
			Zone->DamageZone->OnComponentBeginOverlap.Broadcast(Zone->DamageZone, Character, nullptr, 0, false, FHitResult());
			TestEqual(TEXT("Duplicate overlap cannot double damage"),
				ASC->GetNumericAttribute(UMD_CharacterAttributeSet::GetHealthAttribute()), 92.f);
			ADD_LATENT_AUTOMATION_COMMAND(FMDCoolingTimedDamageCheck(this, World, EntryPump, Zone, Character, ASC));
			bDeferredCleanup = true;
			return true;
		}
	}
	return false;
}
#endif

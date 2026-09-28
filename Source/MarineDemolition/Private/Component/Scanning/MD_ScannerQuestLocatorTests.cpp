#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Character/MD_Character.h"
#include "Component/Scanning/MD_ScannerComponent.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Components/AudioComponent.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UnrealType.h"
#include "World/Sample/MD_QuestItem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDQuestLocatorTest, "MarineDemolition.Scanner.QuestLocator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMDQuestLocatorTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Isolated world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	// Initialize the level so TActorIterator sees active runtime actors. The native
	// uninitialized game instance deliberately excludes project persistence subsystems.
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	FURL URL;
	URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
	if (!World->SetGameMode(URL)) return false;
	World->InitializeActorsForPlay(URL);
	World->BeginPlay();
	AMD_Character* Character = World->SpawnActor<AMD_Character>();
	APlayerController* PC = World->SpawnActor<APlayerController>();
	if (!Character || !PC) return false;
	PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));
	PC->Possess(Character);
	TestTrue(TEXT("Locally controlled test player"), PC->IsLocalController());
	UMD_ScannerComponent* Scanner = Character->GetScannerComponent();
	UMD_EquipHotbarComponent* Hotbar = Character->GetHotbarComponent();
	UMD_ToolDefinition* Tool = LoadObject<UMD_ToolDefinition>(nullptr, TEXT("/Game/Equipment/Tools/DA_Tool_Scanner.DA_Tool_Scanner"));
	if (!Scanner || !Hotbar || !TestNotNull(TEXT("Scanner tool asset"), Tool)) return false;
	// Supply an inventory fixture without touching the user's save or shop/GAS state.
	FArrayProperty* EntriesProperty = FindFProperty<FArrayProperty>(Hotbar->GetClass(), TEXT("ToolEntries"));
	if (!EntriesProperty) return false;
	auto& Entries = *EntriesProperty->ContainerPtrToValuePtr<TArray<FMD_HotbarToolEntry>>(Hotbar);
	Entries.SetNum(1); Entries[0].ToolDefinition = Tool; Entries[0].Count = 1;
	TestTrue(TEXT("Scanner equipped"), Scanner->IsScannerEquipped());
	TestNotNull(TEXT("Authored triple beep loaded"), Scanner->QuestLocatorSound.Get());
	AMD_QuestItem* First = World->SpawnActor<AMD_QuestItem>(FVector(500,0,0), FRotator::ZeroRotator);
	AMD_QuestItem* Second = World->SpawnActor<AMD_QuestItem>(FVector(0,700,0), FRotator::ZeroRotator);
	if (!First || !Second) return false;
	Scanner->UpdateQuestLocator();
	TestNull(TEXT("Unassigned items do not beep"), Scanner->QuestLocatorAudio.Get());
	First->QuestDefinitionId = FPrimaryAssetId(TEXT("Quest"), TEXT("X7_R06"));
	Second->QuestDefinitionId = First->QuestDefinitionId;
	Scanner->UpdateQuestLocator();
	if (!TestNotNull(TEXT("Quest beacon component"), Scanner->QuestLocatorAudio.Get())) return false;
	AMD_QuestItem* InitialTarget = Scanner->QuestLocatorTarget.Get();
	TestTrue(TEXT("Audio follows the sample root"), Scanner->QuestLocatorAudio->GetAttachParent() == InitialTarget->GetRootComponent());
	Scanner->NextQuestLocatorTime = 0; Scanner->UpdateQuestLocator();
	TestTrue(TEXT("Multiple samples take turns"), Scanner->QuestLocatorTarget.Get() != InitialTarget);
	First->SetActorHiddenInGame(true); Second->SetActorLocation(FVector(4000,0,0));
	Scanner->UpdateQuestLocator();
	TestNull(TEXT("Hidden and distant samples are silent"), Scanner->QuestLocatorAudio.Get());
	First->SetActorHiddenInGame(false); Scanner->UpdateQuestLocator();
	TestNotNull(TEXT("Return in range resumes beacon"), Scanner->QuestLocatorAudio.Get());
	Entries[0].ToolDefinition = nullptr; Scanner->HandleHotbarChanged(); Scanner->UpdateQuestLocator();
	TestNull(TEXT("Unequip immediately stops beacon"), Scanner->QuestLocatorAudio.Get());
	Entries[0].ToolDefinition = Tool; Scanner->UpdateQuestLocator();
	First->Destroy(); Scanner->UpdateQuestLocator();
	TestNull(TEXT("Destroying target stops beacon"), Scanner->QuestLocatorAudio.Get());
	FProperty* IdProperty = FindFProperty<FProperty>(AMD_QuestItem::StaticClass(), TEXT("QuestDefinitionId"));
	TestTrue(TEXT("Quest identity replicates to clients"), IdProperty && IdProperty->HasAnyPropertyFlags(CPF_Net));
	Scanner->StopQuestLocator();
	return true;
}
#endif

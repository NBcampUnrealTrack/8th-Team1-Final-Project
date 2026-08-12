#include "Gameplay/Trash/MD_TrashLibrary.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/Score/MD_ScoreTableData.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Gameplay/Trash/MD_TrashContainer.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
#include "World/Trash/MD_TrashBin.h"
#include "World/Trash/MD_TrashDispenser.h"
#include "World/Trash/MD_TrashItem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMD_TrashPolicyAutomationTest,
	"MarineDemolition.Trash.DataDrivenPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMD_TrashPolicyAutomationTest::RunTest(const FString& Parameters)
{
	const AMD_TrashBin* BinDefaults = GetDefault<AMD_TrashBin>();
	TestNotNull(TEXT("TrashBin CDO가 존재한다."), BinDefaults);
	if (BinDefaults)
	{
		TestEqual(TEXT("임시 쓰레기통 기본 용량은 12다."), BinDefaults->GetMaxCapacity(), 12);
	}

	const TArray<FName> TrashKindIds = UMD_TrashLibrary::GetTrashKindIds();
	TestTrue(TEXT("종류 DataTable에 최소 한 행이 있다."), !TrashKindIds.IsEmpty());
	const UMD_ScoreTableData* const ScoreTable = LoadObject<UMD_ScoreTableData>(
		nullptr,
		TEXT("/Game/Data/DA_ScoreTable.DA_ScoreTable"));
	TestNotNull(TEXT("실제 ScoreTable asset을 불러온다."), ScoreTable);
	for (const FName TrashKindId : TrashKindIds)
	{
		FMD_TrashKindRow KindRow;
		FMD_TrashCategoryRow CategoryRow;
		FMD_TrashSortDescriptor Descriptor;
		const bool bFoundKind = UMD_TrashLibrary::FindTrashKindRow(TrashKindId, KindRow);
		const bool bFoundCategory = bFoundKind
			&& UMD_TrashLibrary::FindTrashCategoryRow(KindRow.Category, CategoryRow);
		const bool bBuiltDescriptor =
			UMD_TrashLibrary::GetTrashKindSortDescriptor(TrashKindId, Descriptor);

		TestTrue(*FString::Printf(TEXT("'%s' 종류 행을 찾는다."), *TrashKindId.ToString()), bFoundKind);
		TestTrue(*FString::Printf(TEXT("'%s'의 카테고리가 테이블에 정의돼 있다."), *TrashKindId.ToString()), bFoundCategory);
		TestTrue(*FString::Printf(TEXT("'%s' descriptor가 완성된다."), *TrashKindId.ToString()), bBuiltDescriptor);
		if (!bFoundCategory || !bBuiltDescriptor) continue;

		TestTrue(TEXT("DataTable membership이 유효 카테고리를 결정한다."),
			UMD_TrashLibrary::IsTrashCategory(KindRow.Category));
		TestEqual(
			TEXT("descriptor 환급은 카테고리 기본값과 품목 가산의 합이다."),
			Descriptor.RefundCredits,
			FMath::Max(0, CategoryRow.RefundCredits)
				+ FMath::Max(0, KindRow.RefundBonusCredits));
		if (ScoreTable)
		{
			TestNotEqual(TEXT("정분류 score tag가 실제 ScoreTable 단가에 등록되어 있다."),
				ScoreTable->GetEventValue(Descriptor.CorrectSortScoreTag), 0);
			TestNotEqual(TEXT("오분류 score tag가 실제 ScoreTable 단가에 등록되어 있다."),
				ScoreTable->GetEventValue(Descriptor.MisSortScoreTag), 0);
			if (Descriptor.BonusScoreTag.IsValid())
			{
				TestNotEqual(TEXT("품목 bonus tag가 실제 ScoreTable 단가에 등록되어 있다."),
					ScoreTable->GetEventValue(Descriptor.BonusScoreTag), 0);
			}
		}
	}

	TestFalse(TEXT("Score.Sorting.Bonus 태그는 카테고리 행이 아니다."),
		UMD_TrashLibrary::IsTrashCategory(MDGameplayTags::Score_Sorting_Bonus_Tool));

	FMD_TrashSortDescriptor ManualDescriptor;
	ManualDescriptor.Category = MDGameplayTags::Score_Sorting_Metal;
	ManualDescriptor.CorrectSortScoreTag = MDGameplayTags::Score_Sorting_Metal;
	ManualDescriptor.MisSortScoreTag = MDGameplayTags::Score_Penalty_Missort;
	ManualDescriptor.BonusScoreTag = MDGameplayTags::Score_Sorting_Bonus_Tool;
	ManualDescriptor.RefundCredits = 13;

	const FMD_TrashSortResult CorrectResult = UMD_TrashLibrary::EvaluateSort(
		ManualDescriptor, MDGameplayTags::Score_Sorting_Metal);
	TestTrue(TEXT("같은 칸은 정분류다."), CorrectResult.bCanSort && CorrectResult.bCorrectSort);
	TestEqual(TEXT("정분류 환급을 보존한다."), CorrectResult.RefundCredits, 13);
	TestTrue(TEXT("정분류 품목 보너스를 보존한다."),
		CorrectResult.BonusScoreTag.MatchesTagExact(
			MDGameplayTags::Score_Sorting_Bonus_Tool));

	const FMD_TrashSortResult MisSortResult = UMD_TrashLibrary::EvaluateSort(
		ManualDescriptor, MDGameplayTags::Score_Sorting_General);
	TestTrue(TEXT("다른 유효 칸은 오분류로 처리할 수 있다."),
		MisSortResult.bCanSort && !MisSortResult.bCorrectSort);
	TestEqual(TEXT("오분류는 환급이 없다."), MisSortResult.RefundCredits, 0);
	TestFalse(TEXT("오분류는 정분류 품목 보너스를 주지 않는다."),
		MisSortResult.BonusScoreTag.IsValid());

	// 통째 투입은 총액을 합쳐 이 API를 한 번만 호출한다. delegate 1회 계약도 함께 고정한다.
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UMD_MerchantSubsystem* Merchant = NewObject<UMD_MerchantSubsystem>(GameInstance);
	int32 ChangeNotifications = 0;
	Merchant->OnMerchantStateChanged.AddLambda([&ChangeNotifications]() { ++ChangeNotifications; });
	Merchant->AddCredits(13);
	TestEqual(TEXT("합산 환급이 Merchant 원장에 반영된다."), Merchant->GetCredits(), 13);
	TestEqual(TEXT("합산 환급은 변경 알림을 한 번만 발생시킨다."), ChangeNotifications, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMD_TrashContainerAndDispenserAutomationTest,
	"MarineDemolition.Trash.ContainerAndDispenser",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMD_TrashContainerAndDispenserAutomationTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("자동화 테스트에 GEngine이 존재한다."), GEngine))
	{
		return false;
	}

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		TEXT("MD_TrashIntegrationWorld"),
		EUniqueObjectNameOptions::GloballyUnique);
	UWorld* const TestWorld = UWorld::CreateWorld(
		EWorldType::Game,
		false,
		WorldName,
		GetTransientPackage());
	if (!TestNotNull(TEXT("Trash 통합 테스트 World를 생성한다."), TestWorld))
	{
		return false;
	}

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	TestWorld->AddToRoot();
	WorldContext.SetCurrentWorld(TestWorld);
	TestWorld->InitializeActorsForPlay(FURL());

	auto CleanupTestWorld = [&]()
	{
		GEngine->ShutdownWorldNetDriver(TestWorld);
		TestWorld->DestroyWorld(false);
		TestWorld->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(TestWorld);
		TestWorld->RemoveFromRoot();
	};

	UClass* const BinClass = LoadClass<AMD_TrashBin>(
		nullptr,
		TEXT("/Game/Blueprint/Resource/Trash/BP_MD_TrashBin.BP_MD_TrashBin_C"));
	UClass* const DispenserClass = LoadClass<AMD_TrashDispenser>(
		nullptr,
		TEXT("/Game/Blueprint/Resource/Trash/BP_TrashDispenser.BP_TrashDispenser_C"));
	if (!TestNotNull(TEXT("실제 TrashBin Blueprint를 불러온다."), BinClass)
		|| !TestNotNull(TEXT("실제 TrashDispenser Blueprint를 불러온다."), DispenserClass))
	{
		CleanupTestWorld();
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMD_TrashBin* const Bin = TestWorld->SpawnActor<AMD_TrashBin>(
		BinClass,
		FTransform::Identity,
		SpawnParams);
	if (!TestNotNull(TEXT("용량 검증용 TrashBin을 생성한다."), Bin))
	{
		CleanupTestWorld();
		return false;
	}
	// A bare automation world intentionally has no GameMode. Dispatch only the
	// actors under test so overlap processing is enabled without project subsystem side effects.
	Bin->DispatchBeginPlay();

	UBoxComponent* ContentsVolume = nullptr;
	TInlineComponentArray<UBoxComponent*> BinBoxes(Bin);
	for (UBoxComponent* Box : BinBoxes)
	{
		if (IsValid(Box) && Box->GetFName() == TEXT("ContentsVolume"))
		{
			ContentsVolume = Box;
			break;
		}
	}
	if (!TestNotNull(TEXT("TrashBin ContentsVolume을 찾는다."), ContentsVolume))
	{
		CleanupTestWorld();
		return false;
	}

	// The Blueprint's serialized component defaults may predate the native collision
	// profile. Make this fixture independent of those asset defaults and of gravity.
	if (UPrimitiveComponent* const BinRoot = Cast<UPrimitiveComponent>(Bin->GetRootComponent()))
	{
		BinRoot->SetSimulatePhysics(false);
	}
	ContentsVolume->SetBoxExtent(FVector(100.0, 100.0, 100.0), false);
	ContentsVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ContentsVolume->SetCollisionObjectType(ECC_WorldDynamic);
	ContentsVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	ContentsVolume->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	ContentsVolume->SetGenerateOverlapEvents(true);
	ContentsVolume->RecreatePhysicsState();
	TestTrue(TEXT("TrashBin은 테스트 서버 권한을 가진다."), Bin->HasAuthority());
	TestTrue(TEXT("ContentsVolume은 물리 scene에 등록되어 있다."), ContentsVolume->IsRegistered());
	TestTrue(TEXT("ContentsVolume은 0이 아닌 extent를 가진다."),
		ContentsVolume->GetScaledBoxExtent().GetMin() > KINDA_SMALL_NUMBER);

	const FStructProperty* const AcceptedTagsProperty =
		FindFProperty<FStructProperty>(Bin->GetClass(), TEXT("AcceptedContentTags"));
	const FGameplayTagContainer* const AcceptedTags = AcceptedTagsProperty
		? AcceptedTagsProperty->ContainerPtrToValuePtr<FGameplayTagContainer>(Bin)
		: nullptr;
	TestNotNull(TEXT("TrashBin의 AcceptedContentTags property를 찾는다."), AcceptedTags);
	TestTrue(TEXT("TrashBin은 적어도 하나의 content tag를 수용한다."),
		AcceptedTags && !AcceptedTags->IsEmpty());

	const TArray<FName> KindIds = UMD_TrashLibrary::GetTrashKindIds();
	if (!TestTrue(TEXT("통합 테스트에 사용할 TrashKind가 있다."), !KindIds.IsEmpty()))
	{
		CleanupTestWorld();
		return false;
	}
	TArray<AMD_TrashItem*> SpawnedItems;
	TArray<UBoxComponent*> ItemOverlapShapes;
	for (int32 Index = 0; Index < 13; ++Index)
	{
		FActorSpawnParameters ItemSpawnParams;
		ItemSpawnParams.Name = FName(*FString::Printf(TEXT("MD_TrashCapacityItem_%02d"), Index));
		ItemSpawnParams.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMD_TrashItem* const Item = TestWorld->SpawnActor<AMD_TrashItem>(
			AMD_TrashItem::StaticClass(),
			FTransform(ContentsVolume->GetComponentLocation()),
			ItemSpawnParams);
		if (!TestNotNull(TEXT("용량 검증용 TrashItem을 생성한다."), Item))
		{
			CleanupTestWorld();
			return false;
		}
		UStaticMeshComponent* const ItemMesh =
			Cast<UStaticMeshComponent>(Item->GetRootComponent());
		if (!TestNotNull(TEXT("TrashItem의 물리 root mesh가 존재한다."), ItemMesh))
		{
			CleanupTestWorld();
			return false;
		}
		Item->DispatchBeginPlay();
		Item->SetTrashKindId(KindIds[0]);
		ItemMesh->SetSimulatePhysics(false);
		ItemMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Item->SetActorLocation(
			ContentsVolume->GetComponentLocation(),
			false,
			nullptr,
			ETeleportType::TeleportPhysics);

		UBoxComponent* const ItemOverlapShape = NewObject<UBoxComponent>(
			Item,
			FName(*FString::Printf(TEXT("MD_TrashOverlapShape_%02d"), Index)));
		Item->AddInstanceComponent(ItemOverlapShape);
		ItemOverlapShape->SetupAttachment(ItemMesh);
		ItemOverlapShape->SetRelativeLocation(FVector::ZeroVector);
		ItemOverlapShape->SetBoxExtent(FVector(10.0, 10.0, 10.0), false);
		ItemOverlapShape->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		ItemOverlapShape->SetCollisionObjectType(ECC_PhysicsBody);
		ItemOverlapShape->SetCollisionResponseToAllChannels(ECR_Ignore);
		ItemOverlapShape->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
		ItemOverlapShape->SetGenerateOverlapEvents(true);
		ItemOverlapShape->RegisterComponent();
		ItemOverlapShape->RecreatePhysicsState();
		ItemOverlapShape->UpdateOverlaps();

		if (Index == 0)
		{
			FGameplayTagContainer ItemTags;
			Item->GetOwnedGameplayTags(ItemTags);
			TestTrue(TEXT("TrashItem은 storage content tag를 제공한다."),
				ItemTags.HasTag(MDGameplayTags::Resource_Storage_Content_Trash));

			bool bAcceptedByTagContract = false;
			if (AcceptedTags)
			{
				for (const FGameplayTag& AcceptedTag : *AcceptedTags)
				{
					if (AcceptedTag.MatchesTag(MDGameplayTags::Resource_Storage_Content)
						&& ItemTags.HasTag(AcceptedTag))
					{
						bAcceptedByTagContract = true;
						break;
					}
				}
			}
			TestTrue(TEXT("TrashBin과 TrashItem의 content tag 계약이 일치한다."),
				bAcceptedByTagContract);
			TestTrue(TEXT("item overlap shape가 물리 scene에 등록되어 있다."),
				ItemOverlapShape->IsRegistered());
		}

		SpawnedItems.Add(Item);
		ItemOverlapShapes.Add(ItemOverlapShape);
	}

	ContentsVolume->UpdateOverlaps();
	TestWorld->Tick(LEVELTICK_All, 1.0f / 60.0f);
	ContentsVolume->UpdateOverlaps();
	TArray<UPrimitiveComponent*> OverlappingComponents;
	ContentsVolume->GetOverlappingComponents(OverlappingComponents);
	TestTrue(TEXT("ContentsVolume은 13개 item collision component를 감지한다."),
		OverlappingComponents.Num() >= 13);
	TestTrue(TEXT("ContentsVolume과 첫 item collision component가 실제로 overlap한다."),
		!ItemOverlapShapes.IsEmpty()
		&& ContentsVolume->IsOverlappingComponent(ItemOverlapShapes[0]));
	Bin->RefreshContentsFromOverlaps();
	TestEqual(TEXT("13개 overlap에서도 서버 입고는 MaxCapacity 12로 제한된다."),
		Bin->GetContentCount(), 12);

	TArray<AActor*> CanonicalContents;
	Bin->GetContentsSnapshot(CanonicalContents);
	TArray<FName> ActualNames;
	for (const AActor* Content : CanonicalContents)
	{
		if (IsValid(Content)) ActualNames.Add(Content->GetFName());
	}
	const auto ActorNameStringLess = [](const FName& Left, const FName& Right)
	{
		return Left.ToString() < Right.ToString();
	};
	ActualNames.Sort(ActorNameStringLess);
	TArray<FName> ExpectedNames;
	for (const AMD_TrashItem* Item : SpawnedItems)
	{
		ExpectedNames.Add(Item->GetFName());
	}
	ExpectedNames.Sort(ActorNameStringLess);
	ExpectedNames.SetNum(12);
	TestEqual(TEXT("복원용 refresh가 선택한 actor name 개수는 12개다."),
		ActualNames.Num(), ExpectedNames.Num());
	const int32 ComparableNameCount = FMath::Min(ActualNames.Num(), ExpectedNames.Num());
	for (int32 Index = 0; Index < ComparableNameCount; ++Index)
	{
		TestEqual(
			*FString::Printf(TEXT("복원용 refresh actor name[%d]가 FString 이름순과 일치한다."), Index),
			ActualNames[Index].ToString(),
			ExpectedNames[Index].ToString());
	}

	TArray<AActor*> DrainedContents;
	TestTrue(TEXT("통째 분류 drain은 처음 한 번 성공한다."),
		IMD_TrashContainer::Execute_TryDrainContentsForSorting(Bin, DrainedContents));
	TestEqual(TEXT("drain은 입고된 12개만 Sorter에 넘긴다."), DrainedContents.Num(), 12);
	TestEqual(TEXT("drain 직후 통 원장은 비어 있다."), Bin->GetContentCount(), 0);
	TArray<AActor*> DuplicateDrain;
	TestFalse(TEXT("같은 transaction의 중복 drain은 거부한다."),
		IMD_TrashContainer::Execute_TryDrainContentsForSorting(Bin, DuplicateDrain));
	TestTrue(TEXT("분류 실패 rollback은 통 추적을 복구한다."),
		IMD_TrashContainer::Execute_TryRestoreDrainedContentsAfterFailedSort(
			Bin, DrainedContents));
	TestEqual(TEXT("rollback 뒤 12개 입고가 복구된다."), Bin->GetContentCount(), 12);

	if (!DrainedContents.IsEmpty() && IsValid(DrainedContents[0]))
	{
		DrainedContents[0]->Destroy();
		TestEqual(TEXT("입고품 파괴 시 대기 중 overflow 한 개가 승격되어 용량을 채운다."),
			Bin->GetContentCount(), 12);
	}

	for (AMD_TrashItem* Item : SpawnedItems)
	{
		if (IsValid(Item)) Item->Destroy();
	}
	Bin->Destroy();

	AMD_TrashDispenser* const Dispenser = TestWorld->SpawnActor<AMD_TrashDispenser>(
		DispenserClass,
		FTransform(FVector(10000.0, 0.0, 0.0)),
		SpawnParams);
	if (!TestNotNull(TEXT("보급 제한 검증용 Dispenser를 생성한다."), Dispenser))
	{
		CleanupTestWorld();
		return false;
	}
	Dispenser->DispatchBeginPlay();

	IMD_Interactable::Execute_Interact(Dispenser, nullptr);
	TestEqual(TEXT("첫 요청은 통 하나를 보급한다."),
		Dispenser->GetLiveDispensedBinCount(), 1);
	IMD_Interactable::Execute_Interact(Dispenser, nullptr);
	TestEqual(TEXT("같은 cooldown 안의 재요청은 새 통을 만들지 않는다."),
		Dispenser->GetLiveDispensedBinCount(), 1);

	for (int32 ExpectedCount = 2; ExpectedCount <= 8; ++ExpectedCount)
	{
		for (TActorIterator<AMD_TrashBin> It(TestWorld); It; ++It)
		{
			if (It->GetOwner() == Dispenser)
			{
				It->SetActorLocation(
					FVector(12000.0 + ExpectedCount * 500.0, It->GetUniqueID(), 0.0),
					false,
					nullptr,
					ETeleportType::TeleportPhysics);
			}
		}
		TestWorld->Tick(LEVELTICK_All, 0.3f);
		IMD_Interactable::Execute_Interact(Dispenser, nullptr);
		TestEqual(TEXT("cooldown 뒤에는 MaxLive까지 한 개씩 보급한다."),
			Dispenser->GetLiveDispensedBinCount(), ExpectedCount);
	}

	for (TActorIterator<AMD_TrashBin> It(TestWorld); It; ++It)
	{
		if (It->GetOwner() == Dispenser)
		{
			It->SetActorLocation(FVector(20000.0, It->GetUniqueID(), 0.0));
		}
	}
	TestWorld->Tick(LEVELTICK_All, 0.3f);
	IMD_Interactable::Execute_Interact(Dispenser, nullptr);
	TestEqual(TEXT("MaxLive 8에서는 추가 보급을 거부한다."),
		Dispenser->GetLiveDispensedBinCount(), 8);

	for (TActorIterator<AMD_TrashBin> It(TestWorld); It; ++It)
	{
		if (It->GetOwner() == Dispenser)
		{
			It->Destroy();
			break;
		}
	}
	TestEqual(TEXT("보급된 통이 파괴되면 weak 원장에서 즉시 정리된다."),
		Dispenser->GetLiveDispensedBinCount(), 7);
	CleanupTestWorld();
	return true;
}

#endif

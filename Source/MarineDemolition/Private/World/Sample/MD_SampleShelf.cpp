#include "World/Sample/MD_SampleShelf.h"

#include "Component/Interaction/MD_InteractionComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
// MD_MerchantSubsystem.h는 FGameplayTag를 전방 선언만 한다. 값으로 받으므로 전체 정의가 필요하다.
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "Util/MD_MaterialTintUtility.h"
#include "World/Sample/MD_SampleItem.h"

AMD_SampleShelf::AMD_SampleShelf()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	ShelfMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShelfMesh"));
	ShelfMesh->SetupAttachment(Root);
	// 샘플을 선반 앞까지 들고 와야 하므로 접촉이 불가피하다. 선반에 부딪혔다고 깨지면
	// 회수 자체가 불가능해지므로 물리 바디는 통과시키고 폰만 막는다.
	ShelfMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ShelfMesh->SetCollisionObjectType(ECC_WorldStatic);
	ShelfMesh->SetCollisionResponseToAllChannels(ECR_Block);
	ShelfMesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);

	CollectionVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("CollectionVolume"));
	CollectionVolume->SetupAttachment(Root);
	// 기본 선반 모델(SM_MD_SampleShelf: 깊이 36 × 폭 104 × 높이 111cm)에 맞춘 값이다.
	// 액터 원점은 선반 바닥 중앙이므로 Z를 절반만큼 올려야 볼륨이 바닥 위에 선다.
	// X를 앞으로 조금 밀어 플레이어가 서는 쪽 공간을 덮는다.
	CollectionVolume->SetBoxExtent(FVector(30.f, 55.f, 58.f));
	CollectionVolume->SetRelativeLocation(FVector(12.f, 0.f, 58.f));
	CollectionVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollectionVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	// 샘플 메시는 ECC_PhysicsBody로 잡혀 있다 (AMD_SampleItem 생성자).
	CollectionVolume->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	CollectionVolume->SetGenerateOverlapEvents(true);

	DefaultSlot = CreateDefaultSubobject<UArrowComponent>(TEXT("DefaultSlot"));
	DefaultSlot->SetupAttachment(Root);
	// 기본 선반 모델의 첫 칸 위치: 맨 아래 판 윗면이 Z=9.5cm, 좌측 첫 칸이 Y=-40cm.
	// 여기서 SlotSpacing(Y+40)으로 3칸이 -40 / 0 / +40에 정렬된다.
	DefaultSlot->SetRelativeLocation(FVector(0.f, -40.f, 9.5f));
	DefaultSlot->SetArrowColor(FColor::Green);
	DefaultSlot->ArrowSize = 1.f;
	DefaultSlot->bHiddenInGame = true;
}

void AMD_SampleShelf::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMD_SampleShelf, DisplayEntries);
}

void AMD_SampleShelf::BeginPlay()
{
	Super::BeginPlay();

	CollectionVolume->OnComponentBeginOverlap.AddDynamic(this, &AMD_SampleShelf::OnCollectionBeginOverlap);
	CollectionVolume->OnComponentEndOverlap.AddDynamic(this, &AMD_SampleShelf::OnCollectionEndOverlap);

	// 스냅샷 복원 등으로 이미 볼륨 안에 놓여 있는 퀘스트 아이템을 회수 대상으로 등록하는 처리
	if (HasAuthority() == true)
	{
		TArray<AActor*> Overlapping;
		CollectionVolume->GetOverlappingActors(Overlapping, AMD_QuestItem::StaticClass());
		for (AActor* Actor : Overlapping)
		{
			if (AMD_QuestItem* QuestItem = Cast<AMD_QuestItem>(Actor))
			{
				PendingQuestItems.Add(QuestItem);
			}
		}

		if (PendingQuestItems.Num() > 0)
		{
			GetWorldTimerManager().SetTimer(
				PollTimerHandle, this, &AMD_SampleShelf::PollPendingSamples, PollInterval, true);
		}
	}

	RebuildDisplay();
}

void AMD_SampleShelf::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void AMD_SampleShelf::OnCollectionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (HasAuthority() == false)
	{
		return;
	}

	AMD_QuestItem* QuestItem = Cast<AMD_QuestItem>(OtherActor);
	if (IsValid(QuestItem) == false)
	{
		return;
	}

	PendingQuestItems.Add(QuestItem);

	// 파괴 기능이 있는 파생 아이템만 볼륨 진입 충격을 무시하게 하는 확장 호출
	QuestItem->SuppressImpactDamage(ImpactSuppressDuration);

	// 들고 들어온 경우 지금은 회수하면 안 된다. 손을 떠날 때까지 폴링으로 기다린다.
	if (!GetWorldTimerManager().IsTimerActive(PollTimerHandle))
	{
		GetWorldTimerManager().SetTimer(
			PollTimerHandle, this, &AMD_SampleShelf::PollPendingSamples, PollInterval, true);
	}
}

void AMD_SampleShelf::OnCollectionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (HasAuthority() == false)
	{
		return;
	}

	if (AMD_QuestItem* QuestItem = Cast<AMD_QuestItem>(OtherActor))
	{
		PendingQuestItems.Remove(QuestItem);
	}
}

void AMD_SampleShelf::PollPendingSamples()
{
	if (HasAuthority() == false)
	{
		return;
	}

	TArray<AMD_QuestItem*> CollectableQuestItems;

	for (auto It = PendingQuestItems.CreateIterator(); It; ++It)
	{
		AMD_QuestItem* QuestItem = It->Get();
		if (IsValid(QuestItem) == false || QuestItem->IsActorBeingDestroyed() == true)
		{
			It.RemoveCurrent();
			continue;
		}

		// 파괴 기능이 있는 파생 아이템의 보호가 폴링 사이에 끊기지 않게 하는 갱신
		QuestItem->SuppressImpactDamage(ImpactSuppressDuration);

		if (IsHeldByAnyPawn(QuestItem) == true)
		{
			continue;
		}

		CollectableQuestItems.Add(QuestItem);
		It.RemoveCurrent();
	}

	for (AMD_QuestItem* QuestItem : CollectableQuestItems)
	{
		CollectQuestItem(QuestItem);
	}

	if (PendingQuestItems.Num() == 0)
	{
		GetWorldTimerManager().ClearTimer(PollTimerHandle);
	}
}

bool AMD_SampleShelf::IsHeldByAnyPawn(const AActor* Target) const
{
	UWorld* World = GetWorld();
	if (!IsValid(Target) || !IsValid(World))
	{
		return false;
	}

	AGameStateBase* GameState = World->GetGameState();
	if (!IsValid(GameState))
	{
		return false;
	}

	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (!IsValid(PlayerState))
		{
			continue;
		}

		APawn* Pawn = PlayerState->GetPawn();
		if (!IsValid(Pawn))
		{
			continue;
		}

		UMD_InteractionComponent* Interaction =
			Pawn->FindComponentByClass<UMD_InteractionComponent>();
		if (IsValid(Interaction) && Interaction->GetHeldActor() == Target)
		{
			return true;
		}
	}

	return false;
}

void AMD_SampleShelf::CollectQuestItem(AMD_QuestItem* QuestItem)
{
	if (HasAuthority() == false || IsValid(QuestItem) == false || QuestItem->IsActorBeingDestroyed() == true)
	{
		return;
	}

	UWorld* World = GetWorld();
	UGameInstance* GameInstance = IsValid(World) ? World->GetGameInstance() : nullptr;
	bool bIntact = QuestItem->IsQuestItemIntact();
	UMD_MerchantSubsystem* Merchant = IsValid(GameInstance) ? GameInstance->GetSubsystem<UMD_MerchantSubsystem>() : nullptr;
	AMD_SampleItem* Sample = Cast<AMD_SampleItem>(QuestItem);
	bool bIsDestructibleSample = IsValid(Sample);
	EMD_SampleGrade SampleGrade = bIsDestructibleSample == true ? Sample->Grade : EMD_SampleGrade::C;

	// 아이템 액터 제거 이후에도 선반 전시와 퀘스트 진행에 사용할 최소 정보 복사
	FMD_ShelfDisplayEntry Entry;
	Entry.bDamaged = bIntact == false;
	Entry.Scale = QuestItem->GetActorScale3D();
	UStaticMeshComponent* QuestItemMesh = QuestItem->FindComponentByClass<UStaticMeshComponent>();
	if (IsValid(QuestItemMesh) == true)
	{
		Entry.Mesh = QuestItemMesh->GetStaticMesh();
	}

	// 퀘스트 판정에 실제 액터를 사용하고 제거가 확정된 뒤에만 진행도를 반영하는 제출 처리
	bool bQuestItemRemoved = false;
	if (IsValid(Merchant) == true)
	{
		bQuestItemRemoved = Merchant->TryDeliverQuestItem(QuestItem);
	}
	else
	{
		bQuestItemRemoved = QuestItem->Destroy();
	}

	if (bQuestItemRemoved == false)
	{
		return;
	}

	DisplayEntries.Add(Entry);

	if (bIsDestructibleSample == true)
	{
		UE_LOG(LogTemp, Log, TEXT("[SampleShelf] %s: %s급 샘플 회수 (%s) — 전시 %d건"), *GetName(), *StaticEnum<EMD_SampleGrade>()->GetNameStringByValue(static_cast<int64>(SampleGrade)), bIntact == true ? TEXT("온전") : TEXT("손상"), DisplayEntries.Num());
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[SampleShelf] %s: 파괴 불가 퀘스트 아이템 회수 — 전시 %d건"), *GetName(), DisplayEntries.Num());
	}

	// 리슨 서버는 OnRep가 오지 않으므로 직접 갱신한다.
	RebuildDisplay();
}

void AMD_SampleShelf::OnRep_DisplayEntries()
{
	RebuildDisplay();
}

void AMD_SampleShelf::RebuildDisplay()
{
	for (UStaticMeshComponent* Mesh : DisplayMeshes)
	{
		if (IsValid(Mesh))
		{
			Mesh->DestroyComponent();
		}
	}
	DisplayMeshes.Reset();

	for (int32 Index = 0; Index < DisplayEntries.Num(); ++Index)
	{
		FMD_ShelfDisplayEntry& Entry = DisplayEntries[Index];
		if (!IsValid(Entry.Mesh))
		{
			continue;
		}

		UStaticMeshComponent* Display = NewObject<UStaticMeshComponent>(this);
		if (!IsValid(Display))
		{
			continue;
		}

		Display->SetStaticMesh(Entry.Mesh);
		Display->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Display->SetGenerateOverlapEvents(false);
		Display->RegisterComponent();

		Display->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
		Display->SetWorldTransform(GetSlotTransform(Index));
		Display->SetWorldScale3D(Entry.Scale);

		// 손상 샘플은 원본과 같은 붉은 틴트로 표시한다.
		if (Entry.bDamaged)
		{
			if (UMaterialInstanceDynamic* MID = Display->CreateAndSetMaterialInstanceDynamic(0))
			{
				MDMaterialTint::SetVectorParameterChecked(MID, TEXT("Color"), FLinearColor::Red);
			}
		}

		DisplayMeshes.Add(Display);
	}
}

TArray<FTransform> AMD_SampleShelf::CollectManualSlots() const
{
	TArray<FTransform> Transforms;

	TArray<UArrowComponent*> Arrows;
	GetComponents<UArrowComponent>(Arrows);

	// Arrow가 하나뿐이면 그건 기본 DefaultSlot이므로 수동 배치로 보지 않는다.
	if (Arrows.Num() < 2)
	{
		return Transforms;
	}

	Transforms.Reserve(Arrows.Num());
	for (const UArrowComponent* Arrow : Arrows)
	{
		if (IsValid(Arrow))
		{
			Transforms.Add(Arrow->GetComponentTransform());
		}
	}

	return Transforms;
}

FTransform AMD_SampleShelf::GetSlotTransform(int32 Index) const
{
	// 디자이너가 Arrow를 여러 개 놓았으면 그 배치를 그대로 존중한다.
	TArray<FTransform> ManualSlots = CollectManualSlots();
	if (ManualSlots.Num() > 0)
	{
		FTransform& Slot = ManualSlots[Index % ManualSlots.Num()];
		int32 Row = Index / ManualSlots.Num();
		if (Row == 0)
		{
			return Slot;
		}

		FTransform Result = Slot;
		Result.AddToTranslation(Slot.GetRotation().RotateVector(RowSpacing * Row));
		return Result;
	}

	// 기본 배치: DefaultSlot을 시작점으로 SlotsPerRow만큼 한 줄에 늘어놓고 다음 줄로 내린다.
	USceneComponent* Origin = IsValid(DefaultSlot)
		? static_cast<USceneComponent*>(DefaultSlot)
		: static_cast<USceneComponent*>(Root);
	if (!IsValid(Origin))
	{
		return GetActorTransform();
	}

	int32 PerRow = FMath::Max(1, SlotsPerRow);
	int32 Column = Index % PerRow;
	int32 Row = Index / PerRow;

	FTransform Result = Origin->GetComponentTransform();
	FVector LocalOffset = SlotSpacing * Column + RowSpacing * Row;
	Result.AddToTranslation(Result.GetRotation().RotateVector(LocalOffset));

	return Result;
}

void AMD_SampleShelf::RestorePersistentDisplayEntries(TArray<FMD_ShelfDisplayEntry> Entries)
{
	// 클라이언트의 독립적인 납품 외형 변경 차단
	if (HasAuthority() == false)
	{
		// 서버가 아닌 요청 종료
		return;
	}
	// 납품된 물품을 다시 제출하지 않고 전시 기록만 교체
	DisplayEntries = MoveTemp(Entries);
	// 호스트 전시 컴포넌트 재구성
	OnRep_DisplayEntries();
	// 저장된 전시 목록의 클라이언트 복제 요청
	ForceNetUpdate();
}

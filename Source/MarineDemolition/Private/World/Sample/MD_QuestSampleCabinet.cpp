#include "World/Sample/MD_QuestSampleCabinet.h"
#include "Gameplay/Interaction/MD_InteractionPresentationLibrary.h"
#include "Component/Interaction/MD_InteractionPromptComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Data/Merchant/MD_QuestDefinition.h"
#include "World/Sample/MD_SampleItem.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

AMD_QuestSampleCabinet::AMD_QuestSampleCabinet()
{
    // 인스턴스와 파생 BP가 행동 유형을 선택하는 기본 구성 생성
    InteractionPrompt = CreateDefaultSubobject<UMD_InteractionPromptComponent>(TEXT("InteractionPrompt"));
    // 기존 상호작용 동작에 대응하는 공통 문구 유형 지정
    InteractionPrompt->PromptType = EMD_InteractionPromptType::Open;

    bReplicates = true;
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    CabinetQuestId = FPrimaryAssetId(TEXT("Quest"), TEXT("X7-M03"));
    BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
    BodyMesh->SetupAttachment(Root);
    BodyMesh->SetCollisionProfileName(TEXT("BlockAll"));
    DoorPivot = CreateDefaultSubobject<USceneComponent>(TEXT("DoorPivot"));
    DoorPivot->SetupAttachment(Root);
    DoorPivot->SetRelativeLocation(FVector(-45.5f, 41.5f, 0.f));
    DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
    DoorMesh->SetupAttachment(DoorPivot);
    DoorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GlassMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GlassMesh"));
    GlassMesh->SetupAttachment(DoorPivot);
    GlassMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ClosedDoorBlocker = CreateDefaultSubobject<UBoxComponent>(TEXT("ClosedDoorBlocker"));
    ClosedDoorBlocker->SetupAttachment(Root);
    ClosedDoorBlocker->SetRelativeLocation(FVector(-5.f, 43.f, 87.f));
    ClosedDoorBlocker->SetBoxExtent(FVector(48.f, 5.f, 72.f));
    ClosedDoorBlocker->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    ClosedDoorBlocker->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
    SamplePreview = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SamplePreview"));
    SamplePreview->SetupAttachment(Root);
    SamplePreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DefaultSpawnPoint->SetRelativeLocation(FVector(-5.f, 4.f, 99.f));
    SamplePreview->SetRelativeLocation(DefaultSpawnPoint->GetRelativeLocation());
}

void AMD_QuestSampleCabinet::BeginPlay()
{
    Super::BeginPlay();
    OnRep_Released();
    OnRep_Opened();
}

bool AMD_QuestSampleCabinet::ClaimsQuestPhase(const FPrimaryAssetId& Id, int32 Phase) const
{
    return Id == CabinetQuestId && Phase == SamplePhaseIndex;
}


FText AMD_QuestSampleCabinet::GetInteractionDisplayName_Implementation() const
{
    return FText::FromString(TEXT("격리 B 보안 캐비닛"));
}

FText AMD_QuestSampleCabinet::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
    if (bOpened) return FText::FromString(TEXT("샘플을 조준해 회수"));
    // 캐비닛에 선택된 문 열기 유형의 공통 설정 문구 반환
    return UMD_InteractionPresentationLibrary::FindActorActionPrompt(this, Interactor);
}

bool AMD_QuestSampleCabinet::CanInteract_Implementation(AActor* Interactor) const
{
    // 열린 캐비닛의 중복 상호작용 차단
    return bOpened == false;
}

void AMD_QuestSampleCabinet::Interact_Implementation(AActor* Interactor)
{
    if (!HasAuthority() || !IsValid(Interactor) || !CanInteract_Implementation(Interactor)) return;
    if (FVector::DistSquared(Interactor->GetActorLocation(), ClosedDoorBlocker->Bounds.GetBox().GetClosestPointTo(Interactor->GetActorLocation())) > FMath::Square(260.f)) return;
    bOpened = true;
    OnRep_Opened();
    ForceNetUpdate();
}

void AMD_QuestSampleCabinet::OnRep_Opened()
{
    SetActorTickEnabled(bOpened);
    if (!bOpened)
    {
        OpenAlpha = 0.f;
        DoorPivot->SetRelativeRotation(FRotator::ZeroRotator);
        ClosedDoorBlocker->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    }
}

void AMD_QuestSampleCabinet::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    OpenAlpha = FMath::Min(1.f, OpenAlpha + DeltaSeconds / FMath::Max(.1f, OpenDuration));
    DoorPivot->SetRelativeRotation(FRotator(0.f, OpenYaw * FMath::SmoothStep(0.f, 1.f, OpenAlpha), 0.f));
    if (OpenAlpha < 1.f) return;
    ClosedDoorBlocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (HasAuthority() && !bReleased)
    {
        if (!ReleaseSample())
        {
            // Failed spawn is retryable; never grant a second item on repeated input.
            bOpened = false;
            OnRep_Opened();
            ForceNetUpdate();
        }
    }
    SetActorTickEnabled(false);
}

bool AMD_QuestSampleCabinet::ReleaseSample()
{
    UMD_QuestDefinition* D = UMD_QuestDefinition::LoadDefinition(CabinetQuestId);
    if (!D) return false;
    const TSubclassOf<AMD_QuestItem> ItemClass = ResolveSpawnItemClass(*D, SamplePhaseIndex);
    if (!ItemClass) return false;
    // Restored/carried items and another cabinet must never produce duplicate quest samples.
    for (TActorIterator<AMD_QuestItem> It(GetWorld()); It; ++It)
        if (It->QuestDefinitionId == CabinetQuestId && It->IsA(ItemClass))
        { bReleased = true; OnRep_Released(); ForceNetUpdate(); return true; }
    EMD_SampleGrade Grade = EMD_SampleGrade::C;
    if (D->GetAcceptedGradeTags(SamplePhaseIndex).Num() > 0 && !ResolveSpawnGrade(*D, SamplePhaseIndex, Grade)) return false;
    const FTransform Transform = DefaultSpawnPoint->GetComponentTransform();
    AMD_QuestItem* Item = GetWorld()->SpawnActorDeferred<AMD_QuestItem>(ItemClass, Transform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Item) return false;
    Item->QuestDefinitionId = CabinetQuestId;
    if (AMD_SampleItem* Sample = Cast<AMD_SampleItem>(Item))
    {
        Sample->Grade = Grade;
        Sample->SpawnProtectionDuration = 3.f;
    }
    Item->FinishSpawning(Transform);
    Item->SuppressImpactDamage(3.f);
    bReleased = true; OnRep_Released(); ForceNetUpdate();
    return true;
}

void AMD_QuestSampleCabinet::OnRep_Released()
{
    SamplePreview->SetVisibility(!bReleased);
}

void AMD_QuestSampleCabinet::TrySpawnForQuestState()
{
    // This authored spawner reserves the phase, but releases its item only after opening.
}

void AMD_QuestSampleCabinet::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
    for (UStaticMeshComponent* Mesh : {BodyMesh.Get(), DoorMesh.Get()})
    {
        Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
        Mesh->SetRenderCustomDepth(bHighlighted);
    }
}

void AMD_QuestSampleCabinet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMD_QuestSampleCabinet, bOpened);
    DOREPLIFETIME(AMD_QuestSampleCabinet, bReleased);
}

#include "World/Trash/MD_TrashDispenser.h"

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Util/MD_LogUtility.h"
#include "World/Trash/MD_TrashBin.h"

AMD_TrashDispenser::AMD_TrashDispenser()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetSimulatePhysics(false);

	InteractionVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionVolume"));
	InteractionVolume->SetupAttachment(Root);
	InteractionVolume->SetBoxExtent(FVector(75.f, 75.f, 100.f));
	InteractionVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionVolume->SetCollisionObjectType(ECC_WorldDynamic);
	InteractionVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionVolume->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	InteractionVolume->SetGenerateOverlapEvents(false);
	InteractionVolume->SetCanEverAffectNavigation(false);

	SpawnPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("SpawnPoint"));
	SpawnPoint->SetupAttachment(Root);
	// SM_TrashDispenser 앞쪽에 두고, 통 바닥이 지면과 겹치지 않게 2cm만 띄운다.
	SpawnPoint->SetRelativeLocation(FVector(100.f, 0.f, 2.f));
	SpawnPoint->SetArrowColor(FColor::Cyan);
	SpawnPoint->ArrowSize = 1.5f;
	SpawnPoint->bHiddenInGame = true;

	SpawnClearance = CreateDefaultSubobject<UBoxComponent>(TEXT("SpawnClearance"));
	SpawnClearance->SetupAttachment(SpawnPoint);
	// BP_MD_TrashBin의 0.75 스케일 경계(약 36×32×37cm 반경)에 여유를 더한다.
	SpawnClearance->SetRelativeLocation(FVector(0.f, 0.f, 38.f));
	SpawnClearance->SetBoxExtent(FVector(40.f, 36.f, 38.f));
	SpawnClearance->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpawnClearance->SetGenerateOverlapEvents(false);
	SpawnClearance->SetCanEverAffectNavigation(false);
	SpawnClearance->ShapeColor = FColor::Green;
}

void AMD_TrashDispenser::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (!IsValid(Mesh)) return;

	Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
	Mesh->SetRenderCustomDepth(bHighlighted);
}

bool AMD_TrashDispenser::RequiresServerInteraction_Implementation() const
{
	return true;
}

void AMD_TrashDispenser::Interact_Implementation(AActor* Interactor)
{
	if (!HasAuthority()) return;

	if (!IsValid(SpawnPoint) || !IsValid(SpawnClearance) || !TrashBinClass || !GetWorld())
	{
		UE_LOG(MarineDemolition, Warning,
			TEXT("'%s' 보급기의 SpawnPoint, SpawnClearance 또는 TrashBinClass가 설정되지 않았다."),
			*GetName());
		MulticastDispenseResult(EMD_TrashDispenseResult::NotConfigured);
		return;
	}

	if (!IsSpawnAreaClear())
	{
		MulticastDispenseResult(EMD_TrashDispenseResult::Blocked);
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = Cast<APawn>(Interactor);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
	
	FTransform SpawnTransform = SpawnPoint->GetComponentTransform();
	SpawnTransform.SetScale3D(SpawnTransform.GetScale3D() / GetActorScale3D());

	AMD_TrashBin* SpawnedTrashBin = GetWorld()->SpawnActor<AMD_TrashBin>(
		TrashBinClass,
		SpawnTransform,
		SpawnParams);

	MulticastDispenseResult(IsValid(SpawnedTrashBin)
		? EMD_TrashDispenseResult::Success
		: EMD_TrashDispenseResult::SpawnFailed);
}

bool AMD_TrashDispenser::IsSpawnAreaClear() const
{
	if (!GetWorld() || !IsValid(SpawnClearance)) return false;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TrashDispenserClearance), false, this);
	QueryParams.AddIgnoredActor(this);

	const FCollisionShape ClearanceShape =
		FCollisionShape::MakeBox(SpawnClearance->GetScaledBoxExtent());

	return !GetWorld()->OverlapBlockingTestByProfile(
		SpawnClearance->GetComponentLocation(),
		SpawnClearance->GetComponentQuat(),
		TEXT("PhysicsActor"),
		ClearanceShape,
		QueryParams);
}

void AMD_TrashDispenser::MulticastDispenseResult_Implementation(
	EMD_TrashDispenseResult Result)
{
	OnDispenseResult(Result);
}

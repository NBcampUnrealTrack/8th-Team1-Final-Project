#include "World/Lore/MD_LoreActor.h"

#include "Components/SphereComponent.h"
#include "Data/Lore/MD_LoreCatalog.h"
#include "Engine/GameInstance.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Gameplay/Lore/MD_LoreSubsystem.h"

AMD_LoreActor::AMD_LoreActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(false);

	// 메시의 단순 충돌 유무와 관계없이 기록물을 조준할 수 있도록 하는 전용 쿼리 충돌 설정
	InteractionCollision = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionCollision"));
	SetRootComponent(InteractionCollision);
	InteractionCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionCollision->SetCollisionObjectType(ECC_WorldDynamic);
	InteractionCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionCollision->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);

	// 기록물 외형이 전용 충돌체의 위치와 함께 배치되도록 하는 메시 구성
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(InteractionCollision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

bool AMD_LoreActor::RequiresServerInteraction_Implementation() const
{
	return true;
}

void AMD_LoreActor::Interact_Implementation(AActor* Interactor)
{
	APawn* InteractorPawn = Cast<APawn>(Interactor);
	if (CanCollectLore(InteractorPawn) == false)
	{
		return;
	}

	UMD_LoreSubsystem* LoreSubsystem = GetLoreSubsystem();
	if (IsValid(LoreSubsystem) == false)
	{
		return;
	}

	// 서버 원장이 최초 발견과 개별 및 완성 보상의 단일 확정 지점인 수집 요청
	if (LoreSubsystem->TryDiscoverLore(LoreCatalog, LoreId) == false)
	{
		RefreshCollectionState();
	}
}

void AMD_LoreActor::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (IsValid(Mesh) == false || IsActorBeingDestroyed() == true)
	{
		return;
	}

	Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
	Mesh->SetRenderCustomDepth(bHighlighted);
}

void AMD_LoreActor::BeginPlay()
{
	Super::BeginPlay();

	// 잘못 배치된 기록물이 상호작용 트레이스를 계속 가로막지 않도록 하는 비활성화 처리
	if (IsLoreConfigurationValid() == false)
	{
		InteractionCollision->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Ignore);
		if (HasAuthority() == true)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Lore] 잘못된 로어 수집 액터 설정: %s"), *GetPathName());
		}
		return;
	}

	if (HasAuthority() == false)
	{
		return;
	}

	UMD_LoreSubsystem* LoreSubsystem = GetLoreSubsystem();
	if (IsValid(LoreSubsystem) == false)
	{
		InteractionCollision->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Ignore);
		return;
	}

	// 저장 데이터 적용과 다른 플레이어의 수집을 즉시 반영하기 위한 서버 원장 구독
	LoreProgressChangedHandle = LoreSubsystem->OnLoreProgressChanged.AddUObject(this, &AMD_LoreActor::RefreshCollectionState);
	RefreshCollectionState();
}

void AMD_LoreActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UMD_LoreSubsystem* LoreSubsystem = GetLoreSubsystem();
	if (IsValid(LoreSubsystem) == true)
	{
		LoreSubsystem->OnLoreProgressChanged.Remove(LoreProgressChangedHandle);
	}

	LoreProgressChangedHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

FName AMD_LoreActor::GetLoreId() const
{
	return LoreId;
}

UMD_LoreCatalog* AMD_LoreActor::GetLoreCatalog() const
{
	return LoreCatalog;
}

bool AMD_LoreActor::CanCollectLore(APawn* InteractorPawn) const
{
	if (HasAuthority() == false || IsActorBeingDestroyed() == true || IsValid(InteractorPawn) == false || InteractorPawn->HasAuthority() == false)
	{
		return false;
	}

	AController* InteractorController = InteractorPawn->GetController();
	if (IsValid(InteractorController) == false || InteractorController->GetPawn() != InteractorPawn)
	{
		return false;
	}

	// 인터페이스가 다른 서버 코드에서 직접 호출되어도 원거리 수집을 막기 위한 거리 검사
	float MaximumDistanceSquared = FMath::Square(FMath::Max(1.0f, MaxCollectionDistance));
	float InteractionDistanceSquared = FVector::DistSquared(InteractorPawn->GetActorLocation(), GetActorLocation());
	if (InteractionDistanceSquared > MaximumDistanceSquared)
	{
		return false;
	}

	if (IsLoreConfigurationValid() == false)
	{
		return false;
	}

	UMD_LoreSubsystem* LoreSubsystem = GetLoreSubsystem();
	return IsValid(LoreSubsystem) == true && LoreSubsystem->HasDiscoveredLore(LoreCatalog->SiteId, LoreId) == false;
}

bool AMD_LoreActor::IsLoreConfigurationValid() const
{
	if (IsValid(LoreCatalog) == false || LoreId.IsNone() == true)
	{
		return false;
	}

	TArray<FText> ValidationErrors;
	if (LoreCatalog->ValidateCatalog(ValidationErrors) == false)
	{
		return false;
	}

	// 카탈로그의 실제 정의가 배치 식별자와 연결되는지 확인하는 참조 검사
	FMD_LoreDefinition LoreDefinition;
	return LoreCatalog->FindLore(LoreId, LoreDefinition) == true;
}

UMD_LoreSubsystem* AMD_LoreActor::GetLoreSubsystem() const
{
	UGameInstance* GameInstance = GetGameInstance();
	return IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_LoreSubsystem>() : nullptr;
}

void AMD_LoreActor::RefreshCollectionState()
{
	if (HasAuthority() == false || IsActorBeingDestroyed() == true || IsLoreConfigurationValid() == false)
	{
		return;
	}

	UMD_LoreSubsystem* LoreSubsystem = GetLoreSubsystem();
	if (IsValid(LoreSubsystem) == false || LoreSubsystem->HasDiscoveredLore(LoreCatalog->SiteId, LoreId) == false)
	{
		return;
	}

	// 레벨 재방문과 저장 복원에서도 이미 등록된 기록물을 다시 조사하지 못하게 하는 서버 파괴
	SetInteractionHighlighted_Implementation(false);
	Destroy();
}

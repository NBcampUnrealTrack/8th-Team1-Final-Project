#include "World/Lore/MD_LoreActor.h"

#include "Components/SphereComponent.h"
#include "Data/Lore/MD_LoreDefinition.h"
#include "Engine/GameInstance.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Gameplay/Lore/MD_LoreSubsystem.h"
#include "Gameplay/Lore/MD_LoreProfileSubsystem.h"
#include "TimerManager.h"

AMD_LoreActor::AMD_LoreActor()
{
	PrimaryActorTick.bCanEverTick = false;
	// 각 클라이언트의 독립 배치 액터로 수집과 파괴를 개인에게만 적용
	bReplicates = false;
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
	// 개인 기록만 변경하므로 로컬 입력 경로에서 수집 처리
	return false;
}

bool AMD_LoreActor::CanInteract_Implementation(AActor* Interactor) const
{
	// 조사한 로컬 플레이어의 개인 보유 상태 검사
	return CanCollectLore(Cast<APawn>(Interactor));
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

	// 조사한 본인의 개인 원장에만 발견 기록 반영
	if (LoreSubsystem->TryDiscoverLore(LoreAssetId) == false)
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

FText AMD_LoreActor::GetInteractionDisplayName_Implementation() const
{
	// 할당된 Asset ID에서 상호작용 이름을 제공할 독립 정의 에셋을 찾는다.
	UMD_LoreSubsystem* LoreSubsystem = GetLoreSubsystem();
	UMD_LoreDefinition* LoreDefinition = IsValid(LoreSubsystem) == true ? LoreSubsystem->GetLoreDefinition(LoreAssetId) : nullptr;
	FText InteractionDisplayName = IsValid(LoreDefinition) == true ? LoreDefinition->EntryName : FText::GetEmpty();
	return InteractionDisplayName;
}

void AMD_LoreActor::BeginPlay()
{
	Super::BeginPlay();
	// 유효하지 않은 배치와 개인 사용자가 없는 전용 서버의 조사 차단
	if (IsLoreConfigurationValid() == false || GetNetMode() == NM_DedicatedServer)
	{
		// 무효 기록물이 상호작용 트레이스를 가로막는 상황 방지
		InteractionCollision->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Ignore);
		// 개인 수집 대상이 없는 초기화 종료
		return;
	}
	// 로그인과 로컬 사용자 생성 지연에 따른 초기화 재시도
	GetWorldTimerManager().SetTimer(ProfileReadyTimer, this, &ThisClass::RefreshCollectionState, 0.25f, true);
	// 이미 준비된 개인 기록의 즉시 반영
	RefreshCollectionState();
}

void AMD_LoreActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 파괴된 로컬 액터에 초기화 재시도가 전달되는 상황 방지
	if (IsValid(GetWorld()) == true)
	{
		// 월드 해제 이후 중복 종료에서도 안전한 타이머 정리
		GetWorldTimerManager().ClearTimer(ProfileReadyTimer);
	}
	// 맵 이동 이후 이전 기록물의 개인 원장 구독 정리
	if (LocalProfile.IsValid() == true)
	{
		// 이 액터의 수집 상태 구독만 해제
		LocalProfile->OnPersonalLoreChanged.Remove(LoreProgressChangedHandle);
	}
	Super::EndPlay(EndPlayReason);
}

bool AMD_LoreActor::CanCollectLore(APawn* InteractorPawn) const
{
	// 다른 참가자의 입력과 이미 제거 중인 기록물의 조사 차단
	if (IsActorBeingDestroyed() == true || IsValid(InteractorPawn) == false || InteractorPawn->IsLocallyControlled() == false)
	{
		return false;
	}

	AController* InteractorController = InteractorPawn->GetController();
	if (IsValid(InteractorController) == false || InteractorController->GetPawn() != InteractorPawn)
	{
		return false;
	}

	// 로컬 인터페이스를 직접 호출해도 원거리 기록을 수집하지 않는 거리 검사
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
	if (IsValid(LoreSubsystem) == false)
	{
		return false;
	}

	// 로드되지 않은 개인 기록을 빈 목록으로 간주하는 상황 방지
	UMD_LoreProfileSubsystem* Profile = LoreSubsystem->GetLocalProfile();
	// 계정별 최초 로드 완료와 개인 미보유 상태를 함께 검사
	return IsValid(Profile) == true && Profile->IsLoaded() == true && Profile->GetDiscoveredLoreAssetIds().Contains(LoreAssetId) == false;
}

bool AMD_LoreActor::IsLoreConfigurationValid() const
{
	// 비어 있는 Asset ID는 어떤 로어 정의도 나타내지 않는다.
	if (LoreAssetId.IsValid() == false)
	{
		return false;
	}

	UMD_LoreSubsystem* LoreSubsystem = GetLoreSubsystem();
	if (IsValid(LoreSubsystem) == false)
	{
		return false;
	}

	// 할당된 Asset ID가 유효한 로어 정의로 해석되는지 확인한다.
	UMD_LoreDefinition* LoreDefinition = LoreSubsystem->GetLoreDefinition(LoreAssetId);
	bool bHasValidLoreDefinition = IsValid(LoreDefinition) == true;
	if (bHasValidLoreDefinition == true)
	{
		bHasValidLoreDefinition = LoreDefinition->IsValidDefinition();
	}
	return bHasValidLoreDefinition;
}

UMD_LoreSubsystem* AMD_LoreActor::GetLoreSubsystem() const
{
	UGameInstance* GameInstance = GetGameInstance();
	return IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_LoreSubsystem>() : nullptr;
}

void AMD_LoreActor::RefreshCollectionState()
{
	// 개인 수집으로 이미 제거 중인 액터의 재진입 차단
	if (IsActorBeingDestroyed() == true)
	{
		// 중복 파괴 요청 종료
		return;
	}
	// 현재 월드에서 사용하는 개인 기록 접근 경로 조회
	UMD_LoreSubsystem* Lore = GetLoreSubsystem();
	// 로컬 사용자 생성 이전의 접근 보류
	if (IsValid(Lore) == false)
	{
		// 다음 초기화 재시도까지 대기
		return;
	}
	// 실제 개인 파일을 소유하는 로컬 원장 조회
	UMD_LoreProfileSubsystem* Profile = Lore->GetLocalProfile();
	// 계정과 저장 파일이 준비될 때까지 개인 보유 판정 보류
	if (IsValid(Profile) == false || Profile->EnsureLoaded() == false)
	{
		// 다른 사용자의 상태로 배치물을 제거하는 상황 방지
		return;
	}
	// 개인 파일 복원 이후 초기화 재시도 종료
	GetWorldTimerManager().ClearTimer(ProfileReadyTimer);
	// 이후 동일 기록의 개인 수집 이벤트 구독
	if (LocalProfile.IsValid() == false)
	{
		// 종료 시 구독 해제에 사용할 개인 원장 보관
		LocalProfile = Profile;
		// 같은 개인에게 배치된 중복 기록물도 함께 정리
		LoreProgressChangedHandle = Profile->OnPersonalLoreChanged.AddUObject(this, &ThisClass::RefreshCollectionState);
	}
	// 본인이 이미 보유한 기록만 현재 월드에서 제거
	if (Profile->GetDiscoveredLoreAssetIds().Contains(LoreAssetId) == true)
	{
		// 수집 직전 활성 하이라이트 정리
		SetInteractionHighlighted_Implementation(false);
		// 비복제 액터의 로컬 파괴로 다른 참가자의 기록물 보존
		Destroy();
		// 파괴 이후 표시 변경 방지
		return;
	}
}

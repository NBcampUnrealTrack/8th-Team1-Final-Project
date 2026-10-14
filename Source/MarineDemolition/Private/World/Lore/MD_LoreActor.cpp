#include "World/Lore/MD_LoreActor.h"
#include "Component/Interaction/MD_InteractionPromptComponent.h"

#include "Components/SphereComponent.h"
#include "Data/Lore/MD_LoreDefinition.h"
#include "Engine/GameInstance.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Gameplay/Lore/MD_LoreSubsystem.h"
#include "Gameplay/Lore/MD_LoreProfileSubsystem.h"
#include "TimerManager.h"

AMD_LoreActor::AMD_LoreActor()
{
	// 인스턴스와 파생 BP가 행동 유형을 선택하는 기본 구성 생성
	InteractionPrompt = CreateDefaultSubobject<UMD_InteractionPromptComponent>(TEXT("InteractionPrompt"));
	// 기존 상호작용 동작에 대응하는 공통 문구 유형 지정
	InteractionPrompt->PromptType = EMD_InteractionPromptType::Read;

	PrimaryActorTick.bCanEverTick = false;
	// 기존 개인 기록에 따른 표시 상태를 각 월드에서 독립 관리
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
	// 로컬 입력에서 소유 컨트롤러의 팀 습득 요청으로 연결
	return false;
}

bool AMD_LoreActor::CanInteract_Implementation(AActor* Interactor) const
{
	// 입력한 본인의 미보유 기록만 조사 대상으로 허용
	APawn* InteractorPawn = Cast<APawn>(Interactor);
	// 원격 입력과 유효하지 않은 배치 또는 거리 차단
	if (CanCollectLore(InteractorPawn) == false || InteractorPawn->IsLocallyControlled() == false)
	{
		// 개인 상호작용 대상에서 제외
		return false;
	}
	// 초기화 때 연결한 개인 원장을 재사용하여 반복 조회 방지
	UMD_LoreProfileSubsystem* Profile = LocalProfile.Get();
	// 목록 복사 없이 최초 로드 완료와 개인 미보유 상태 확인
	return IsValid(Profile) == true && Profile->IsLoaded() == true && Profile->HasDiscoveredLore(LoreAssetId) == false;
}

void AMD_LoreActor::Interact_Implementation(AActor* Interactor)
{
	// 기존 개인 이력과 상호작용 조건을 확인한 팀 습득 요청
	if (CanInteract_Implementation(Interactor) == false)
	{
		return;
	}

	UMD_LoreSubsystem* LoreSubsystem = GetLoreSubsystem();
	if (IsValid(LoreSubsystem) == false)
	{
		return;
	}

	// 서버가 확인한 새 기록을 현재 참가자 각자의 원장에 반영
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
	// 이미 준비된 개인 기록의 즉시 반영
	RefreshCollectionState();
	// 개인 원장 준비가 지연된 배치물에만 재시도 타이머 사용
	if (LocalProfile.IsValid() == false && IsActorBeingDestroyed() == false)
	{
		// 로그인 또는 로컬 사용자 생성 이후 초기화 재시도
		GetWorldTimerManager().SetTimer(ProfileReadyTimer, this, &ThisClass::RefreshCollectionState, 0.25f, true);
	}
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
	// 서버에서도 실제 요청자의 폰과 살아 있는 배치물 확인
	if (IsActorBeingDestroyed() == true || IsValid(InteractorPawn) == false || InteractorPawn->GetWorld() != GetWorld())
	{
		return false;
	}

	AController* InteractorController = InteractorPawn->GetController();
	if (IsValid(InteractorController) == false || InteractorController->GetPawn() != InteractorPawn)
	{
		return false;
	}

	// 클라이언트가 보낸 식별자와 무관하게 실제 서버 배치 위치로 거리 검사
	float MaximumDistanceSquared = FMath::Square(FMath::Max(1.0f, MaxCollectionDistance));
	float InteractionDistanceSquared = FVector::DistSquared(InteractorPawn->GetActorLocation(), GetActorLocation());
	if (InteractionDistanceSquared > MaximumDistanceSquared)
	{
		return false;
	}

	// 호스트의 기존 수집 이력과 무관하게 실제 정의의 유효성 확인
	return IsLoreConfigurationValid();
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
	// 이미 연결된 개인 원장을 수집 이벤트에서도 재사용
	UMD_LoreProfileSubsystem* Profile = LocalProfile.Get();
	// 최초 연결 또는 원장 해제 이후에만 서브시스템 재조회
	if (IsValid(Profile) == false)
	{
		// 현재 월드의 개인 원장 접근 경로 확인
		UMD_LoreSubsystem* Lore = GetLoreSubsystem();
		// 로컬 사용자 생성 이전의 접근 보류
		if (IsValid(Lore) == false)
		{
			// 다음 초기화 재시도까지 대기
			return;
		}
		// 실제 개인 파일을 소유하는 로컬 원장 연결 준비
		Profile = Lore->GetLocalProfile();
	}
	// 계정과 저장 파일이 준비될 때까지 개인 보유 판정 보류
	if (IsValid(Profile) == false || Profile->EnsureLoaded() == false)
	{
		// 다른 사용자의 상태로 배치물을 제거하는 상황 방지
		return;
	}
	// 최초 연결 때만 타이머 정리와 수집 이벤트 구독 수행
	if (LocalProfile.IsValid() == false)
	{
		// 개인 파일 복원 이후 초기화 재시도 종료
		GetWorldTimerManager().ClearTimer(ProfileReadyTimer);
		// 종료 시 구독 해제에 사용할 개인 원장 보관
		LocalProfile = Profile;
		// 같은 개인에게 배치된 중복 기록물도 함께 정리
		LoreProgressChangedHandle = Profile->OnPersonalLoreChanged.AddUObject(this, &ThisClass::RefreshCollectionState);
	}
	// 본인이 이미 보유한 기록만 현재 월드에서 제거
	if (Profile->HasDiscoveredLore(LoreAssetId) == true)
	{
		// 수집 완료된 배치물의 불필요한 후속 알림 처리 중단
		Profile->OnPersonalLoreChanged.Remove(LoreProgressChangedHandle);
		// 종료 시 중복 구독 해제 방지
		LoreProgressChangedHandle.Reset();
		// 수집 직전 활성 하이라이트 정리
		SetInteractionHighlighted_Implementation(false);
		// 호스트가 수집한 배치물도 다른 참가자의 요청 검증에 계속 사용
		if (GetNetMode() == NM_ListenServer)
		{
			// 호스트 화면에서 이미 보유한 기록물 제거
			SetActorHiddenInGame(true);
			// 호스트의 조준과 이동에 남은 배치물이 개입하는 상황 방지
			SetActorEnableCollision(false);
		}
		else
		{
			// 서버 검증에 사용하지 않는 로컬 사본 제거
			Destroy();
		}
		// 파괴 이후 표시 변경 방지
		return;
	}
}

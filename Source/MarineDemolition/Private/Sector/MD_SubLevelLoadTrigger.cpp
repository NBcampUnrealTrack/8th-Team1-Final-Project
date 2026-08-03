// Fill out your copyright notice in the Description page of Project Settings.


#include "Sector/MD_SubLevelLoadTrigger.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Components/BoxComponent.h"
#include "Net/UnrealNetwork.h"
#include "MD_LogUtility.h"

// Sets default values
AMD_SubLevelLoadTrigger::AMD_SubLevelLoadTrigger()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	SetReplicates(true);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(FName("DefaultSceneRoot"));
	SetRootComponent(SceneRoot);

	BoxCollision = CreateDefaultSubobject<UBoxComponent>(FName("BoxCollision"));
	BoxCollision->SetupAttachment(RootComponent);
	// Pawn 종류만 Overlap 이벤트를 생성하도록 설정
	BoxCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BoxCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	BoxCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	BoxCollision->SetGenerateOverlapEvents(true);
}

void AMD_SubLevelLoadTrigger::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, bShouldBeLoaded);
}

// Called when the game starts or when spawned
void AMD_SubLevelLoadTrigger::BeginPlay()
{
	Super::BeginPlay();

	// BeginOverlap 이벤트 연결
	BoxCollision->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::OnCollisionBeginOverlap);

	// 현재 Level Streaming 상태 맞게 초기화
	ApplyStreamingState();
}

bool AMD_SubLevelLoadTrigger::IsLevelLoaded() const
{
	return IsValid(StreamedLevelInstance) && StreamedLevelInstance->IsLevelLoaded();
}

void AMD_SubLevelLoadTrigger::OnCollisionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Server 환경 확인 및 이전에 Level Streaming 요청이 처리되었는지 확인
	if (HasAuthority() == false || bShouldBeLoaded == true)
	{
		return;
	}

	// Overlap 객체가 Local Pawn(플레이어 캐릭터)이 아니면 함수 조기 종료
	APawn* OtherPawn = Cast<APawn>(OtherActor);
	if (IsValid(OtherPawn) == false || OtherPawn->IsPlayerControlled() == false)
	{
		return;
	}

	// Level Streaming 플래그 값 변경
	bShouldBeLoaded = true;

	// Listen Server 환경 고려 플래그 값 변경 함수 실행
	OnRep_ShouldBeLoaded();

	// 즉시 동기화
	ForceNetUpdate();
}

void AMD_SubLevelLoadTrigger::OnRep_ShouldBeLoaded()
{
	// 현재 Level Streaming 상태 동기화
	ApplyStreamingState();
}

void AMD_SubLevelLoadTrigger::ApplyStreamingState()
{
	// Server의 상태와 Local의 상태가 동일하면 함수 조기 종료
	if (bShouldBeLoaded == IsLevelLoaded())
	{
		return;
	}

	if (bShouldBeLoaded == true)
	{
		LoadSubLevel();
	}
	else
	{
		UnloadSubLevel();
	}
}

bool AMD_SubLevelLoadTrigger::LoadSubLevel()
{
	// 이미 설정된 Sub Level이 로드된 상태인 경우 함수 조기 종료
	if (IsLevelLoaded() == true)
	{
		return true;
	}

	// Load 되어야 하는 맵이 존재하지 않으면 함수 조기 종료
	if (SubLevelAsset.IsNull() == true)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Warning, TEXT("%s 레벨 스트리밍에 사용되는 Map이 설정되지 않았습니다."), *UE_CALL_LOCATIONINFO);
		return false;
	}

	if (IsValid(StreamedLevelInstance) == false)
	{
		// Streaming 되는 레벨의 위치 및 회전 값 설정 (Scale은 { 1, 1, 1 } 고정)
		FTransform LevelTransform = FTransform(StreamingRotation, StreamingLocation, FVector::OneVector);
		bool bWasSuccess = false;

		// Sub Level Load 실행
		// 설정된 Level에 대하여 Streaming하도록 월드에 등록 및 로딩 요청
		StreamedLevelInstance = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(this, SubLevelAsset, LevelTransform, bWasSuccess, LevelInstanceName);

		// Level Streaming 실패 또는 Streaming된 Level Instance가 유효하지 않으면 함수 조기 종료
		if (bWasSuccess == false || IsValid(StreamedLevelInstance) == false)
		{
			// 로그 출력
			UE_LOG(MarineDemolition, Warning, TEXT("%s Level Streaming 요청이 실패하였습니다. Level=%s"), *UE_CALL_LOCATIONINFO, *SubLevelAsset.ToString());
			return false;
		}

	}

	// Streaming 된 Level이 반드시 로드 및 렌더링되도록 명시
	StreamedLevelInstance->SetShouldBeLoaded(true);
	StreamedLevelInstance->SetShouldBeVisible(true);

	// 로그 출력
	UE_LOG(MarineDemolition, Log, TEXT("%s Level Streaming 요청에 성공하였습니다. Level=%s, Origin=%s"), *UE_CALL_LOCATIONINFO, *SubLevelAsset.ToString(), *StreamingLocation.ToString());
	return true;
}

bool AMD_SubLevelLoadTrigger::UnloadSubLevel()
{
	// 이미 설정된 Sub Level이 메모리 해제된 상태인 경우 함수 조기 종료
	if (IsLevelLoaded() == false)
	{
		return true;
	};

	StreamedLevelInstance->SetShouldBeVisible(false);
	StreamedLevelInstance->SetShouldBeLoaded(false);

	return true;
}


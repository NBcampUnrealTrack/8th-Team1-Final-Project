// Fill out your copyright notice in the Description page of Project Settings.


#include "Sector/MD_LevelStreamingTrigger.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Components/BoxComponent.h"
#include "Net/UnrealNetwork.h"
#include "MD_LogUtility.h"

// Sets default values
AMD_LevelStreamingTrigger::AMD_LevelStreamingTrigger()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(FName("DefaultSceneRoot"));
	SetRootComponent(SceneRoot);

	BoxCollision = CreateDefaultSubobject<UBoxComponent>(FName("BoxCollision"));
	BoxCollision->SetupAttachment(RootComponent);
	BoxCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BoxCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	BoxCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void AMD_LevelStreamingTrigger::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, bShouldLoad);
}

// Called when the game starts or when spawned
void AMD_LevelStreamingTrigger::BeginPlay()
{
	Super::BeginPlay();

	// BeginOverlap 이벤트 연결
	BoxCollision->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::OnCollisionBeginOverlap);

	// 현재 Level Streaming 상태 동기화
	ApplyStreamingState();
}

void AMD_LevelStreamingTrigger::OnCollisionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Server 환경 확인 및 이전에 Level Streaming 요청이 처리되었는지 확인
	if (HasAuthority() == false || bShouldLoad == true)
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
	bShouldLoad = true;

	// Listen Server 환경 고려 플래그 값 변경 함수 실행
	OnRep_ShouldLoad();

	// 즉시 동기화
	ForceNetUpdate();
}

void AMD_LevelStreamingTrigger::OnRep_ShouldLoad()
{
	// 현재 Level Streaming 상태 동기화
	ApplyStreamingState();
}

void AMD_LevelStreamingTrigger::ApplyStreamingState()
{
	// 서버측에서 Level Streaming 요청이 없거나, 이미 로컬에서 Streaming 되었다면 함수 조기 종료
	if (bShouldLoad == false || bLoadApplied == true)
	{
		return;
	}

	// Streaming 되어야 하는 맵이 존재하지 않으면 함수 조기 종료
	if (StreamingMap.IsNull() == true)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s No streaming level is configured."), *UE_CALL_LOCATIONINFO);
		return;
	}

	bool bWasSuccess = false;
	// Streaming 되는 레벨의 위치에 현재 액터의 좌표, 회전 정보 사용(Scale 제외)
	FTransform LevelTransform(GetActorRotation(), GetActorLocation(), FVector::OneVector);

	// Level Streaming 실행
	// 설정된 Level 에셋에 대하여 Streaming하도록 월드에 등록 및 로딩 요청
	StreamedLevelInstance = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(this, StreamingMap, LevelTransform, bWasSuccess, LevelInstanceName);

	// Level Streaming 실패 또는 Streaming된 Level Instance가 유효하지 않으면 함수 조기 종료
	if (bWasSuccess == false || IsValid(StreamedLevelInstance) == false)
	{
		// 로그 출력
		UE_LOG(MarineDemolition, Error, TEXT("%s Level Streaming 요청이 실패하였습니다. Level=%s"), *UE_CALL_LOCATIONINFO, *StreamingMap.ToString());
		return;
	}

	// 로컬에서 Level Streaming 완료 표시
	bLoadApplied = true;

	// Streaming 된 Level이 반드시 로드되도록 로드하도록 명시
	StreamedLevelInstance->SetShouldBeLoaded(true);
	StreamedLevelInstance->SetShouldBeVisible(true);

	// 로그 출력
	UE_LOG(MarineDemolition, Log, TEXT("%s Level Streaming 요청에 성공하였습니다. Level=%s, Origin=%s"), *UE_CALL_LOCATIONINFO, *StreamingMap.ToString(), *GetActorLocation().ToString());
}


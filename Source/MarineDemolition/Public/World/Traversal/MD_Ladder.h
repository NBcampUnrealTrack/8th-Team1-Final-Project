// 프로젝트 코드

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MD_Ladder.generated.h"

class UArrowComponent;
class UBoxComponent;

/** 사다리와 캐릭터 사이의 오버랩 변화를 외부 이동 시스템에 전달하는 이벤트 형식 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMDLadderCharacterOverlapSignature, AMD_Ladder*, Ladder, AMD_Character*, Character);

/** 등반 가능한 공간과 이동 기준점을 제공하는 사다리 월드 액터 */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API AMD_Ladder : public AActor
{
	GENERATED_BODY()

public:
	/** 기본 등반 공간과 기준점을 구성하는 생성자 */
	AMD_Ladder();

protected:
	/** 캐릭터 오버랩 감지를 연결하는 초기화 함수 */
	virtual void BeginPlay() override;

public:
	/** 캐릭터가 사다리 등반 공간에 진입했음을 서버에서 전달하는 이벤트 */
	UPROPERTY(BlueprintAssignable, Category = "MD|Ladder|Event")
	FMDLadderCharacterOverlapSignature OnCharacterEntered;

	/** 캐릭터가 사다리 등반 공간에서 이탈했음을 서버에서 전달하는 이벤트 */
	UPROPERTY(BlueprintAssignable, Category = "MD|Ladder|Event")
	FMDLadderCharacterOverlapSignature OnCharacterExited;

protected:
	/** 사다리 구성요소의 공통 변환 기준을 제공하는 루트 컴포넌트 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Ladder|Components")
	TObjectPtr<USceneComponent> SceneRoot;

	/** 레벨에서 사다리 외형을 표시하는 정적 메시 컴포넌트 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Ladder|Components")
	TObjectPtr<UStaticMeshComponent> LadderMesh;

	/** 캐릭터의 사다리 진입과 이탈을 감지하는 오버랩 컴포넌트 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Ladder|Components")
	TObjectPtr<UBoxComponent> ClimbTriggerBox;

	/** 캐릭터 캡슐 중심이 내려갈 수 있는 최하단 기준점 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Ladder|Components")
	TObjectPtr<USceneComponent> BottomClimbPoint;

	/** 캐릭터 캡슐 중심이 올라갈 수 있는 최상단 기준점 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Ladder|Components")
	TObjectPtr<USceneComponent> TopClimbPoint;

	/** 상단 올라타기 완료 후 캐릭터가 배치될 기준점 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Ladder|Components")
	TObjectPtr<USceneComponent> TopExitPoint;

	/** 점프 이탈과 캐릭터 스냅 간격에 사용할 사다리 바깥 방향 표시기 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Ladder|Components")
	TObjectPtr<UArrowComponent> OutwardDirection;

public:
	/** 사다리 정적 메시 컴포넌트를 반환하는 조회 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Ladder")
	FORCEINLINE UStaticMeshComponent* GetLadderMesh() const { return LadderMesh; }

	/** 사다리 오버랩 컴포넌트를 반환하는 조회 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Ladder")
	FORCEINLINE UBoxComponent* GetClimbTrigger() const { return ClimbTriggerBox; }

	/** 하단에서 상단을 향하는 정규화된 등반 축 반환 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Ladder")
	FVector GetClimbUpVector() const;

	/** 사다리 표면에서 캐릭터 쪽을 향하는 정규화된 바깥 방향 반환 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Ladder")
	FVector GetOutwardVector() const;

	/** 캐릭터 캡슐 중심용 최하단 월드 위치 반환 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Ladder")
	FVector GetBottomClimbPointLocation() const;

	/** 캐릭터 캡슐 중심용 최상단 월드 위치 반환 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Ladder")
	FVector GetTopClimbPointLocation() const;

	/** 상단 올라타기 완료용 월드 위치 반환 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Ladder")
	FVector GetTopExitPointLocation() const;

	/** 하단과 상단 사이의 유효 등반 길이 반환 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Ladder")
	float GetClimbLength() const;

	/** 월드 위치를 사다리 축에 투영한 정규화 진행도 계산 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Ladder")
	float GetClimbProgress(const FVector& WorldLocation) const;

	/** 월드 위치가 허용 오차를 포함한 상하단 범위 안인지 확인하는 함수 
	* 임의 월드 위치에 대하여 사다리를 이용 가능한 위치인지 여부를 반환하는 함수
	*/
	UFUNCTION(BlueprintPure, Category = "MD|Ladder")
	bool IsWithinClimbBounds(const FVector& WorldLocation, float BoundaryTolerance = 0.0f) const;

	/** 월드 위치를 등반 축에 맞추고 바깥 방향 간격을 적용한 스냅 위치 계산 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Ladder")
	FVector GetClosestClimbLocation(const FVector& WorldLocation, float StandOffDistance) const;

	/** 지정 캐릭터가 현재 등반 오버랩 공간 안에 있는지 확인하는 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Ladder")
	bool IsCharacterInsideTrigger(APawn* Pawn) const;

private:
	/** 캡슐이 등반 공간에 들어온 캐릭터의 진입 이벤트를 전달하는 함수 */
	UFUNCTION()
	void HandleClimbTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** 캡슐이 등반 공간에서 나간 캐릭터의 이탈 이벤트를 전달하는 함수 */
	UFUNCTION()
	void HandleClimbTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex);
};

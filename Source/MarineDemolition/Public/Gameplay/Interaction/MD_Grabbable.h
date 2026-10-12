#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MD_Grabbable.generated.h"

class UPrimitiveComponent;
class USceneComponent;
class APawn;

/** A grabbable may keep the normal carry target, supply its own, or end the grip. */
UENUM(BlueprintType)
enum class EMD_PhysicsHoldTargetResult : uint8
{
	Default,
	Resolved,
	Release
};

/** PhysicsHandle이 물리 픽업을 운반할 때 적용할 설정 묶음 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_HoldProfile
{
	GENERATED_BODY()

	/** 잡은 컴포넌트의 회전을 HoldPoint 목표 회전에 맞출지 여부. false면 Angular 값을 쓰지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle")
	bool bConstrainRotation = true;

	/** 위치 제약에 스프링 드라이브를 사용할지 여부. false면 목표 위치에 거의 즉시 고정된다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle")
	bool bSoftLinear = true;

	/** 회전 제약에 스프링 드라이브를 사용할지 여부. false면 목표 회전에 거의 즉시 고정된다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle",
		meta = (EditCondition = "bConstrainRotation"))
	bool bSoftAngular = true;

	/** 목표 Transform을 InterpolationSpeed로 보간할지 여부. 급격한 입력이 물체에 그대로 튀지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle")
	bool bInterpolate = true;

	/** 목표 위치로 끌어당기는 스프링 강도. 높이면 손 위치에 빠르게 붙는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle",
		meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bSoftLinear"))
	float LinearStiffness = 750.f;

	/** 위치 이동 속도를 깎는 감쇠. 높이면 진동이 빨리 잦아든다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle",
		meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bSoftLinear"))
	float LinearDamping = 200.f;

	/** 목표 회전으로 맞추는 스프링 강도. 낮추면 회전이 한 박자 늦게 따라와 무게감이 생긴다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle",
		meta = (ClampMin = "0.0", UIMin = "0.0",
			EditCondition = "bConstrainRotation && bSoftAngular"))
	float AngularStiffness = 1500.f;

	/** 회전 속도를 깎는 감쇠. 낮으면 오래 출렁여 보관함 내용물이 쏟아지기 쉬워진다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle",
		meta = (ClampMin = "0.0", UIMin = "0.0",
			EditCondition = "bConstrainRotation && bSoftAngular"))
	float AngularDamping = 500.f;

	/** 목표 Transform이 HoldPoint를 따라가는 속도. 낮추면 부드럽지만 반응이 느려진다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle",
		meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bInterpolate"))
	float InterpolationSpeed = 50.f;

	/** 비권한 머신에는 스프링이 없다. 이 속도가 위 Stiffness/Damping의 무게감을 대신한다. 0이면 즉시 붙음 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RemoteFollowSpeed = 12.f;
};

/** 물리 픽업 시 잡을 컴포넌트와 위치를 명시적으로 제공하는 계약 */
UINTERFACE(MinimalAPI, BlueprintType)
class UMD_Grabbable : public UInterface
{
	GENERATED_BODY()
};

class MARINEDEMOLITION_API IMD_Grabbable
{
	GENERATED_BODY()

public:
	/** Validate and prepare object-specific grip state before creating the server physics handle. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction|Physics Pickup")
	bool PreparePhysicsGrip(APawn* Interactor);
	virtual bool PreparePhysicsGrip_Implementation(APawn* Interactor) { return true; }

	/** Override the carry target without requiring the player to know a concrete actor class. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction|Physics Pickup")
	EMD_PhysicsHoldTargetResult ResolvePhysicsHoldTarget(APawn* Interactor, FTransform& OutTarget) const;
	virtual EMD_PhysicsHoldTargetResult ResolvePhysicsHoldTarget_Implementation(
		APawn* Interactor, FTransform& OutTarget) const { return EMD_PhysicsHoldTargetResult::Default; }

	/** PhysicsHandle이 실제로 잡을 물리 시뮬레이션 컴포넌트 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction|Physics Pickup")
	UPrimitiveComponent* GetGrabComponent() const;

	/** PhysicsHandle 제약을 생성할 월드 위치를 제공하는 SceneComponent. 물체의 어디를 잡는지를 결정한다. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction|Physics Pickup")
	USceneComponent* GetGrabPoint() const;

	/** 캐릭터 HoldPoint의 로컬 축을 기준으로 적용할 위치 오프셋(cm). 잡은 물체를 몸 기준 어디에 둘지 결정한다. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction|Physics Pickup")
	FVector GetHoldOffset() const;

	/** 이 물체를 운반하는 동안 적용할 PhysicsHandle 설정 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction|Physics Pickup")
	FMD_HoldProfile GetHoldProfile() const;

	/** HoldPoint로 끌려가기 시작. 가속이 커서 담고 있는 내용물은 여기서 잡아둬야 한다. 서버 전용 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction|Physics Pickup")
	void OnCarryStarted();
	virtual void OnCarryStarted_Implementation() {}

	/** HoldPoint 도달. OnCarryStarted에서 멈춰둔 것을 되돌린다 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction|Physics Pickup")
	void OnCarrySettled();
	virtual void OnCarrySettled_Implementation() {}

	/** 운반 종료. 안정화 전에 놓을 수 있어 여기서도 되돌린다 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction|Physics Pickup")
	void OnCarryEnded();
	virtual void OnCarryEnded_Implementation() {}
};

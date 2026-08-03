#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MD_Grabbable.generated.h"

class UPrimitiveComponent;
class USceneComponent;

/**
 * PhysicsHandle이 물리 픽업을 운반할 때 적용할 설정 묶음.
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_HoldProfile
{
	GENERATED_BODY()

	/**
	 * 잡은 컴포넌트의 회전을 HoldPoint 목표 회전에 맞출지 여부.
	 * true면 위치와 회전을 함께 제어하므로 통이 세워진 자세를 유지하려 한다.
	 * false면 위치만 제어해 잡은 지점을 축으로 자유롭게 흔들리며,
	 * 아래 Angular 계열 값은 사용되지 않는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle")
	bool bConstrainRotation = true;

	/**
	 * 위치 제약에 스프링 드라이브를 사용할지 여부.
	 * true면 LinearStiffness/LinearDamping으로 목표 위치까지 탄성 있게 끌어당긴다.
	 * false면 강체 제약이라 목표 위치에 거의 즉시 고정되고 무게감이 사라진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle")
	bool bSoftLinear = true;

	/**
	 * 회전 제약에 스프링 드라이브를 사용할지 여부.
	 * true면 AngularStiffness/AngularDamping으로 목표 회전을 부드럽게 따라간다.
	 * false면 회전이 목표에 곧바로 고정돼 기울임과 흔들림이 사라진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle",
		meta = (EditCondition = "bConstrainRotation"))
	bool bSoftAngular = true;

	/**
	 * PhysicsHandle 목표 Transform을 보간할지 여부.
	 * true면 HoldPoint가 급하게 움직여도 목표가 InterpolationSpeed로 뒤따라가므로
	 * 빠른 카메라 회전 같은 급격한 입력이 물체에 그대로 튀지 않는다.
	 * false면 목표가 매 프레임 즉시 갱신된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle")
	bool bInterpolate = true;

	/**
	 * 목표 위치로 끌어당기는 스프링 강도.
	 * 높이면 손 위치에 빠르게 붙고, 낮추면 뒤처지며 출렁인다.
	 * 과하게 높이면 물체가 벽에 끼었을 때 밀어내는 힘이 커져 물리가 폭주할 수 있다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle",
		meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bSoftLinear"))
	float LinearStiffness = 750.f;

	/**
	 * 목표 위치로 향하는 속도를 깎는 감쇠.
	 * 높이면 목표를 지나치는 정도와 진동이 줄어 빨리 안정되고,
	 * 낮으면 급정지한 뒤에도 앞뒤로 계속 흔들린다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle",
		meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bSoftLinear"))
	float LinearDamping = 200.f;

	/**
	 * 목표 회전으로 맞추는 회전 스프링 강도.
	 * 높이면 몸이나 카메라 회전에 즉시 따라붙고, 낮추면 한 박자 늦게 돌아와 무게감이 생긴다.
	 * 회전 지연이 지나치면 여기부터 올린다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle",
		meta = (ClampMin = "0.0", UIMin = "0.0",
			EditCondition = "bConstrainRotation && bSoftAngular"))
	float AngularStiffness = 1500.f;

	/**
	 * 회전 속도를 깎는 감쇠.
	 * 높이면 회전을 멈춘 뒤 좌우로 덜 흔들리고 빨리 원래 자세를 되찾는다.
	 * 낮으면 오래 출렁여 보관함 내용물이 쏟아지기 쉬워진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle",
		meta = (ClampMin = "0.0", UIMin = "0.0",
			EditCondition = "bConstrainRotation && bSoftAngular"))
	float AngularDamping = 500.f;

	/**
	 * PhysicsHandle 내부 목표 Transform이 HoldPoint가 지시한 목표를 따라가는 속도.
	 * 낮추면 입력이 한 번 더 완충돼 부드럽지만 반응이 느려지고,
	 * 높이면 보간하지 않는 것과 거의 같아진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle",
		meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bInterpolate"))
	float InterpolationSpeed = 50.f;
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
	/** PhysicsHandle이 실제로 잡을 물리 시뮬레이션 컴포넌트 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction|Physics Pickup")
	UPrimitiveComponent* GetGrabComponent() const;

	/**
	 * PhysicsHandle 제약을 생성할 월드 위치를 제공하는 SceneComponent.
	 * 물체의 어느 지점을 손으로 잡는지를 결정한다. 무게중심보다 위를 잡으면
	 * 본체가 아래에 매달려 흔들리고, 무게중심에 가까울수록 지렛대 효과가 줄어든다.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction|Physics Pickup")
	USceneComponent* GetGrabPoint() const;

	/**
	 * 캐릭터 HoldPoint의 로컬 축을 기준으로 적용할 위치 오프셋.
	 * GrabPoint가 "물체의 어디를 잡는지"라면, 이 값은 "잡은 물체를 몸 기준 어디에 두는지"다.
	 * X는 HoldPoint 전방, Y는 오른쪽, Z는 위쪽이며 단위는 cm다.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction|Physics Pickup")
	FVector GetHoldOffset() const;

	/** 이 물체를 운반하는 동안 적용할 PhysicsHandle 설정 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction|Physics Pickup")
	FMD_HoldProfile GetHoldProfile() const;
};

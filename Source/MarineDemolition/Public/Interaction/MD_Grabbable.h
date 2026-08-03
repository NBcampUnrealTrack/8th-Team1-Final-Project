#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MD_Grabbable.generated.h"

class UPrimitiveComponent;
class USceneComponent;

/** PhysicsHandle이 물리 픽업을 운반할 때 적용할 설정 묶음 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_HoldProfile
{
	GENERATED_BODY()

	/** 잡은 컴포넌트의 회전을 HoldPoint 목표 회전에 맞출지 여부 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle")
	bool bConstrainRotation = true;

	/** 위치 제약에 스프링 드라이브를 사용할지 여부 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle")
	bool bSoftLinear = true;

	/** 회전 제약에 스프링 드라이브를 사용할지 여부 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle",
		meta = (EditCondition = "bConstrainRotation"))
	bool bSoftAngular = true;

	/** PhysicsHandle 목표 Transform을 보간할지 여부 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle")
	bool bInterpolate = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle",
		meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bSoftLinear"))
	float LinearStiffness = 750.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle",
		meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bSoftLinear"))
	float LinearDamping = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle",
		meta = (ClampMin = "0.0", UIMin = "0.0",
			EditCondition = "bConstrainRotation && bSoftAngular"))
	float AngularStiffness = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Handle",
		meta = (ClampMin = "0.0", UIMin = "0.0",
			EditCondition = "bConstrainRotation && bSoftAngular"))
	float AngularDamping = 500.f;

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

	/** PhysicsHandle 제약을 생성할 월드 위치를 제공하는 SceneComponent */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction|Physics Pickup")
	USceneComponent* GetGrabPoint() const;

	/** 캐릭터 HoldPoint의 로컬 축을 기준으로 적용할 위치 오프셋 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction|Physics Pickup")
	FVector GetHoldOffset() const;

	/** 이 물체를 운반하는 동안 적용할 PhysicsHandle 설정 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Interaction|Physics Pickup")
	FMD_HoldProfile GetHoldProfile() const;
};

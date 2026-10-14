// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MD_ComfortComponent.generated.h"

class UCameraComponent;

/**
 * 3D 멀미(시뮬레이터 멀미) 완화를 로컬 플레이어에게만 적용하는 컴포넌트.
 * 수치는 UMD_ComfortSettings(Project Settings -> Game -> MD Comfort)가 들고,
 * 이 컴포넌트는 그 값을 카메라·프레임·시야 입력에 반영하는 일만 한다.
 *
 * 원격 플레이어와 서버 전용 인스턴스에서는 BeginPlay에서 스스로 Tick을 끈다.
 */
UCLASS(ClassGroup = (MD), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_ComfortComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_ComfortComponent();

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * 마우스 시야 입력에 응답 커브를 적용한 값을 돌려준다.
	 * 호출한 쪽은 반환값을 그대로 AddControllerYawInput/PitchInput에 넘기면 된다.
	 *
	 * LookSmoothingHalfLife가 0보다 크면 커브를 먹인 입력을 내부에 누적하고 빈 벡터를 돌려준다.
	 * 누적분은 TickComponent가 반감기에 맞춰 나눠서 직접 적용하므로, 마우스를 놓은 뒤에도
	 * 남은 회전량이 끊기지 않고 흘러나가며 총 회전량은 보존된다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MD|Comfort")
	FVector2D ShapeLookInput(const FVector2D& RawLookInput);

	/** A fixed desk view must not inherit delayed rotations from ordinary look input. */
	void ResetPendingLookInput();

protected:
	virtual void BeginPlay() override;

private:
	/** 로컬 플레이어 카메라에 모션 블러·시야각 설정을 반영한다. */
	void ApplyCameraSettings() const;

	/** 프레임 상한과 VSync 설정을 반영한다. */
	void ApplyFramePacingSettings() const;

	/** 누적된 시야 입력을 반감기에 맞춰 조금씩 흘려보낸다. */
	void DrainPendingLookInput(float DeltaTime);

	/** 소유 Pawn의 카메라 컴포넌트. 없으면 null. */
	UCameraComponent* FindOwnerCamera() const;

	/** 소유 Pawn을 보는 로컬 PlayerController. 원격/서버 전용이면 null. */
	class APlayerController* GetLocalPlayerController() const;

private:
	/** 스무딩이 켜져 있을 때 아직 적용하지 않고 남아 있는 시야 회전량. */
	FVector2D PendingLookInput = FVector2D::ZeroVector;
};

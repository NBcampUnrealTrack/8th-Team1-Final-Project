#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MD_SupportBrace.generated.h"

class AMD_CuttableWall;

// 설치된 임시 지지대. 비주얼 + 어느 벽을 지지 중인지 기록
UCLASS()
class MARINEDEMOLITION_API AMD_SupportBrace : public AActor
{
	GENERATED_BODY()

public:
	AMD_SupportBrace();

	void SetSupportedWall(AMD_CuttableWall* Wall) { SupportedWall = Wall; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Support")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// 이 지지대가 붙잡고 있는 지주격벽
	UPROPERTY(VisibleInstanceOnly, Category = "Support")
	TObjectPtr<AMD_CuttableWall> SupportedWall;
};

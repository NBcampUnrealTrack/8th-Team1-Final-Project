#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MD_Scannable.generated.h"

class AActor;

/** 스캐너에 탐지됐을 때 각 액터가 고유한 시각 반응을 실행하는 공통 계약. */
UINTERFACE(MinimalAPI, BlueprintType)
class UMD_Scannable : public UInterface
{
	GENERATED_BODY()
};

class MARINEDEMOLITION_API IMD_Scannable
{
	GENERATED_BODY()

public:
	/**
	 * 로컬 스캔 하이라이트를 요청한다.
	 * 스캔 어빌리티가 로컬 조종 Pawn에 대해서만 호출하며, 구현체는 Duration 뒤 효과를 해제해야 한다.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Scanning")
	void OnScanned(float Duration, AActor* Scanner);
	virtual void OnScanned_Implementation(float Duration, AActor* Scanner) {}
};

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MD_InteractionPresentationLibrary.generated.h"

/** 대상 액터의 기존 데이터와 공용 이름표를 한 경로로 해석한다. */
UCLASS()
class MARINEDEMOLITION_API UMD_InteractionPresentationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 공용 이름표에서 ID에 해당하는 표시 이름을 찾는다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Interaction")
	static bool FindDisplayName(FName InteractionId, FText& OutDisplayName);

	/** 대상 고유 데이터가 있으면 우선 사용하고, 없으면 대상 클래스에 맞는 공용 이름을 반환한다. */
	UFUNCTION(BlueprintPure, Category = "MD|Interaction")
	static FText ResolveInteractionDisplayName(const UObject* Target);

private:
	/** 클래스 이름 앞의 프로젝트 접두사(MD_)를 제거한 공용 이름표 ID를 만든다. */
	static FName MakeShortInteractionId(const UClass* TargetClass);
};

#include "Util/MD_MaterialTintUtility.h"

#include "Materials/MaterialInstanceDynamic.h"
#include "Util/MD_LogUtility.h"

bool MDMaterialTint::SetVectorParameterChecked(
	UMaterialInstanceDynamic* Material,
	const FName ParameterName,
	const FLinearColor& Value)
{
	if (!Material || ParameterName.IsNone())
	{
		return false;
	}

	FLinearColor UnusedCurrentValue;
	if (!Material->GetVectorParameterValue(FHashedMaterialParameterInfo(ParameterName), UnusedCurrentValue))
	{
		// 동적 인스턴스 이름은 매번 달라지므로 원본 머티리얼 이름으로 중복을 판단한다
		const FString SourceName = GetNameSafe(Material->Parent);

		static TSet<FString> AlreadyWarned;
		const FString WarnKey = FString::Printf(TEXT("%s:%s"), *SourceName, *ParameterName.ToString());

		if (!AlreadyWarned.Contains(WarnKey))
		{
			AlreadyWarned.Add(WarnKey);
			UE_LOG(MarineDemolition, Warning,
				TEXT("머티리얼 '%s'에 벡터 파라미터 '%s'가 없어 색을 적용하지 못했다."),
				*SourceName, *ParameterName.ToString());
		}

		return false;
	}

	Material->SetVectorParameterValue(ParameterName, Value);
	return true;
}

bool MDMaterialTint::SetScalarParameterChecked(
	UMaterialInstanceDynamic* Material,
	const FName ParameterName,
	const float Value)
{
	if (!Material || ParameterName.IsNone())
	{
		return false;
	}

	float UnusedCurrentValue = 0.0f;
	if (!Material->GetScalarParameterValue(FHashedMaterialParameterInfo(ParameterName), UnusedCurrentValue))
	{
		// 동적 인스턴스 이름은 매번 달라지므로 원본 머티리얼 이름으로 중복을 판단한다
		const FString SourceName = GetNameSafe(Material->Parent);

		static TSet<FString> AlreadyWarned;
		const FString WarnKey = FString::Printf(TEXT("%s:%s"), *SourceName, *ParameterName.ToString());

		if (!AlreadyWarned.Contains(WarnKey))
		{
			AlreadyWarned.Add(WarnKey);
			UE_LOG(MarineDemolition, Warning,
				TEXT("머티리얼 '%s'에 스칼라 파라미터 '%s'가 없어 값을 적용하지 못했다."),
				*SourceName, *ParameterName.ToString());
		}

		return false;
	}

	Material->SetScalarParameterValue(ParameterName, Value);
	return true;
}

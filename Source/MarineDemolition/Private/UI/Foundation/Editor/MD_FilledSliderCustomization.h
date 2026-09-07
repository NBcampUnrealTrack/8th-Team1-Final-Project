#pragma once

// 런타임 빌드에서 디테일 편집 의존성 제외
#if WITH_EDITOR
#include "CoreMinimal.h"
#include "IDetailCustomization.h"

// 기존 Slider Style 내부에 채움 브러시를 함께 배치하는 디테일 구성
class FMD_FilledSliderCustomization : public IDetailCustomization
{
public:
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

public:
	// 에디터 등록용 디테일 인스턴스 생성
	static TSharedRef<IDetailCustomization> MakeInstance();
};
#endif

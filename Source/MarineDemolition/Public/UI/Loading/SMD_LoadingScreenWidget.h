#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "Types/UI/MD_LoadingScreenTypes.h"
#include "Widgets/SLeafWidget.h"

class UTexture2D;

/**
 * 로딩 화면 본체. UMG(UUserWidget)가 아니라 Slate인 이유는 두 가지다.
 *   1) UMG 위젯은 옛 월드 소속이라 하드 트래블 때 함께 파괴된다.
 *   2) MoviePlayer는 게임 스레드가 막힌 동안 전용 스레드로 Slate만 직접 그린다.
 *
 * 상태를 갖지 않고 넘겨받은 FMD_LoadingVisualState의 경과 시간만 보고 매 프레임 다시 그린다.
 * 그래서 viewport용과 MoviePlayer용 인스턴스를 각각 만들어도 같은 상태를 공유하면
 * 하나의 연속된 연출로 보인다.
 */
class SMD_LoadingScreenWidget : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMD_LoadingScreenWidget) {}
		SLATE_ARGUMENT(TSharedPtr<FMD_LoadingVisualState>, VisualState)
		/** 미지정이면 물방울을 그리지 않고 배경과 문구만 남는다. */
		SLATE_ARGUMENT(UTexture2D*, BubbleTexture)
		/** 미지정이면 파도형 진행 트랙만 그리고 배는 생략한다. */
		SLATE_ARGUMENT(UTexture2D*, ShipTexture)
		/** 미지정이면 수평선 아래 너울 띠를 생략한다. */
		SLATE_ARGUMENT(UTexture2D*, WaveTexture)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// 화면 전체를 덮으므로 실제 배치 크기는 부모가 정한다. 스케일은 OnPaint에서 계산한다.
	virtual FVector2D ComputeDesiredSize(float) const override;

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	/** 화면 중앙 기준 문구 한 줄. OnPaint가 표로 만들어 한 번에 넘긴다. */
	struct FTextLayer
	{
		FText Text;
		FSlateFontInfo Font;
		FLinearColor Color;
		float AnchorY, TextScale, OffsetY;
	};

	void DrawBubble(const FMD_LoadingDesignSpace& Space, FSlateWindowElementList& DrawElements, int32 LayerId,
		const FVector2D& DesignPosition, const FVector2D& BaseSize, float Scale, float Opacity) const;
	void DrawLoopBubbles(const FMD_LoadingDesignSpace& Space, FSlateWindowElementList& DrawElements, int32 LayerId,
		float Time, float Opacity) const;
	void DrawCompletionCheck(const FMD_LoadingDesignSpace& Space, FSlateWindowElementList& DrawElements, int32 LayerId,
		float Progress, float Opacity) const;
	void DrawVoyageProgress(const FMD_LoadingDesignSpace& Space, FSlateWindowElementList& DrawElements, int32 LayerId,
		float Time, float Progress, float Opacity) const;
	void DrawSkyAndSea(const FMD_LoadingDesignSpace& Space, FSlateWindowElementList& DrawElements, int32 LayerId,
		float Opacity) const;
	void DrawCloudBands(const FMD_LoadingDesignSpace& Space, FSlateWindowElementList& DrawElements, int32 LayerId,
		float Drift, float Opacity) const;
	void DrawIslands(const FMD_LoadingDesignSpace& Space, FSlateWindowElementList& DrawElements, int32 LayerId,
		float Drift, float Opacity) const;
	void DrawSeaSwell(const FMD_LoadingDesignSpace& Space, FSlateWindowElementList& DrawElements, int32 LayerId,
		float Drift, float Opacity) const;
	void DrawCenteredText(const FMD_LoadingDesignSpace& Space, FSlateWindowElementList& DrawElements, int32 LayerId,
		const FTextLayer& Layer) const;

	EActiveTimerReturnType RefreshPaint(double, float);

	TSharedPtr<FMD_LoadingVisualState> VisualState;
	FSlateBrush BubbleBrush;
	FSlateBrush ShipBrush;
	FSlateBrush WaveBrush;
	FSlateFontInfo TitleFont;
	FSlateFontInfo SubtitleFont;
};

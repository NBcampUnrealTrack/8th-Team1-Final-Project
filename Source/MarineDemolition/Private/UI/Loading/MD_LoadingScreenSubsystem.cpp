#include "UI/Loading/MD_LoadingScreenSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LevelStreaming.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "ContentStreaming.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "MoviePlayer.h"
#include "Online/Session/MD_MultiplayManager.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"
#include "Util/MD_LogUtility.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/SLeafWidget.h"

#define LOCTEXT_NAMESPACE "MDLoadingScreen"

namespace MD_LoadingScreenPrivate
{
	// 게임플레이 UMG(HUD/메뉴)보다 반드시 위에 그려져야 하므로 의도적으로 큰 값을 쓴다.
	constexpr int32 ViewportZOrder = 10000;

	// 로딩 연출은 다음 순서로 고정된다.
	//   Intro 0.90초(화면을 완전히 덮음) → Loading loop 2.15초 무한 반복
	//   → 준비 완료 시 Completion 0.85초 → Outro 1.10초 fade-out
	// Intro가 끝나기 전에는 트래블을 시작하지 않는다(RunAfterIntro).
	constexpr double IntroDurationSeconds = 0.90;
	constexpr double CompletionDisplaySeconds = 0.85;
	constexpr double LoadingLoopDurationSeconds = 2.15;
	constexpr double OutroDurationSeconds = 1.10;

	// 준비 판정 폴링 주기와, 종료를 허용하기 위해 요구하는 연속 통과 횟수.
	// 0.05초 x 10회 = 약 0.5초 동안 안정된 상태가 유지돼야 로딩을 닫는다.
	// 한 프레임만 조건을 만족했다가 곧바로 다시 로딩이 시작되는 경우를 거르기 위한 장치다.
	constexpr float ReadinessPollSeconds = 0.05f;
	constexpr int32 RequiredReadyStablePolls = 10;
	// 스트리밍 레벨 누락이나 render asset 요청이 영영 해소되지 않을 때
	// 로딩 화면이 무한히 남는 것을 막는 상한. 초과하면 사유를 남기고 안전하게 닫는다.
	constexpr double ReadinessTimeoutSeconds = 45.0;
	// viewport 오버레이와 MoviePlayer는 서로 스케일 기준이 달라, 해상도에 따라
	// 폰트 크기와 물방울 위치가 트래블 순간 튀었다. 모든 좌표를 이 고정 디자인
	// 해상도 기준으로 계산한 뒤 실제 크기에 맞춰 균일 스케일해서 양쪽을 일치시킨다.
	const FVector2D DesignSize(1920.0f, 1080.0f);

	// 경로가 하드코딩돼 있으므로 에셋을 옮기면 물방울이 그려지지 않는다.
	const TCHAR* BubbleTexturePath =
		TEXT("/Game/UI/Screen/Loading/Texture/T_TransitionBubble.T_TransitionBubble");

	/**
	 * viewport 오버레이 위젯과 MoviePlayer 위젯이 "함께 참조하는" 단일 연출 상태다.
	 *
	 * 트래블 전후로 화면을 담당하는 위젯이 교체되는데, 두 위젯이 같은 인스턴스를 보게 해서
	 * 애니메이션이 끊기거나 처음부터 다시 재생되지 않게 한다. 이 공유 구조가 로딩 연출이
	 * 이어져 보이는 이유이므로, 한쪽 위젯만 보고 수정하면 트래블 순간 연출이 점프한다.
	 *
	 * 경과 시간을 world time이나 timer가 아니라 FPlatformTime::Cycles64()로 재는 이유:
	 * 하드 트래블 중에는 게임 스레드가 LoadMap 안에서 멈춰 있어 world time이 흐르지 않는다.
	 * MoviePlayer의 전용 Slate 스레드는 그동안에도 계속 그려야 하므로 wall-clock이 필요하다.
	 *
	 * 같은 이유로 Completion/Outro 시작 시각은 TAtomic이다. 게임 스레드가 쓰고
	 * MoviePlayer 로딩 스레드가 읽는, 스레드가 다른 값이기 때문이다.
	 */
	struct FLoadingVisualState
	{
		explicit FLoadingVisualState(const FText& InSubtitle, const FText& InCompletionSubtitle)
			: Subtitle(InSubtitle)
			, CompletionSubtitle(InCompletionSubtitle)
			, StartCycles(FPlatformTime::Cycles64())
		{
		}

		double GetElapsedSeconds() const
		{
			return FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartCycles);
		}

		void BeginOutro()
		{
			if (OutroStartCycles.Load() == 0)
			{
				OutroStartCycles.Store(FPlatformTime::Cycles64());
			}
		}

		void BeginCompletion()
		{
			if (CompletionStartCycles.Load() == 0)
			{
				CompletionStartCycles.Store(FPlatformTime::Cycles64());
			}
		}

		bool IsCompletionVisible() const
		{
			return CompletionStartCycles.Load() != 0;
		}

		float GetCompletionElapsedSeconds() const
		{
			const uint64 CompletionCycles = CompletionStartCycles.Load();
			return CompletionCycles == 0
				? 0.0f
				: static_cast<float>(FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - CompletionCycles));
		}

		float GetOverallOpacity() const
		{
			const uint64 OutroCycles = OutroStartCycles.Load();
			if (OutroCycles == 0)
			{
				return 1.0f;
			}

			const double OutroElapsed = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - OutroCycles);
			return 1.0f - FMath::SmoothStep(
				0.0f,
				1.0f,
				FMath::Clamp(static_cast<float>(OutroElapsed / OutroDurationSeconds), 0.0f, 1.0f));
		}

		FText Subtitle;
		FText CompletionSubtitle;
		uint64 StartCycles;
		TAtomic<uint64> CompletionStartCycles{0};
		TAtomic<uint64> OutroStartCycles{0};
	};

	/**
	 * 진행 중 문구를 같은 상황의 완료 문구로 바꾼다.
	 * 호출자가 완료 문구를 따로 넘기지 않아도 되도록 진행 문구의 영문 접두사로 매칭한다.
	 * 즉 호출부(MD_MultiplayManager / MD_GameModeBase)에서 진행 문구를 바꾸면
	 * 여기의 접두사도 같이 고쳐야 하며, 안 고치면 조용히 기본 문구로 떨어진다.
	 */
	FText MakeCompletionSubtitle(const FText& Subtitle)
	{
		const FString Source = Subtitle.ToString();
		if (Source.StartsWith(TEXT("CREW ASSEMBLING")))
		{
			return LOCTEXT("CrewAssembled", "CREW ASSEMBLED • 잠수팀 준비 완료");
		}
		if (Source.StartsWith(TEXT("DIVE IN PROGRESS")))
		{
			return LOCTEXT("DiveReady", "DIVE READY • 잠수 준비 완료");
		}
		if (Source.StartsWith(TEXT("RETURNING TO SURFACE")))
		{
			return LOCTEXT("SurfaceReady", "SURFACE ROUTE READY • 수면 복귀 준비 완료");
		}
		if (Source.StartsWith(TEXT("MISSION INCOMPLETE")))
		{
			return LOCTEXT("ReturnReady", "RETURN ROUTE READY • 복귀 준비 완료");
		}
		if (Source.StartsWith(TEXT("LEAVING THE CREW")))
		{
			return LOCTEXT("DepartureReady", "DEPARTURE READY • 팀 이탈 준비 완료");
		}
		if (Source.StartsWith(TEXT("NEXT SHIFT PREPARING")))
		{
			return LOCTEXT("NextShiftReady", "NEXT SHIFT READY • 다음 작업일 준비 완료");
		}
		return LOCTEXT("LoadingComplete", "LOADING COMPLETE • 로딩 완료");
	}

	struct FBubbleMotion
	{
		FVector2D Position;
		FVector2D Size;
		float FadeInStart;
		float FadeInEnd;
		float FadeOutEnd;
		float RiseMidTime;
		float RiseMidY;
		float RiseEndTime;
		float RiseEndY;
	};

	// 기존 UMG 연출(Anim_TransitionEnd)의 실제 키프레임에서 옮겨온 값이다.
	// 좌표는 화면 중앙 기준 오프셋이고 시간은 2.15초 루프 안에서의 초 단위다.
	// 손으로 맞춘 수치이므로 루프 길이를 바꾸면 물방울 타이밍도 함께 조정해야 한다.
	static const FBubbleMotion LoopBubbles[] =
	{
		{{24.0, 320.0}, {60.0, 60.0}, 0.6333f, 1.1500f, 1.8167f, 1.1167f, -45.0f, 2.0000f, -70.0f},
		{{60.0, 300.0}, {30.0, 30.0}, 0.5167f, 1.4500f, 1.7833f, 1.0833f, -55.0f, 1.9333f, -90.0f},
		{{ 2.0, 290.0}, {15.0, 15.0}, 0.7667f, 1.4167f, 1.9500f, 1.3500f, -40.0f, 2.0667f, -80.0f},
		{{40.0, 280.0}, { 7.0,  7.0}, 0.5167f, 1.4833f, 2.0000f, 1.3500f, -63.0f, 2.1000f, -103.0f},
		{{17.0, 275.0}, {23.0, 23.0}, 0.7833f, 1.4167f, 2.1333f, 1.4167f, -56.0f, 2.1333f, -96.0f},
	};

	float SmoothRange(float Time, float Start, float End)
	{
		if (End <= Start)
		{
			return Time >= End ? 1.0f : 0.0f;
		}
		return FMath::SmoothStep(
			0.0f,
			1.0f,
			FMath::Clamp((Time - Start) / (End - Start), 0.0f, 1.0f));
	}

	/**
	 * 로딩 화면 본체. UMG(UUserWidget)가 아니라 Slate인 이유는 두 가지다.
	 *   1) UMG 위젯은 UObject이자 옛 월드 소속이라 하드 트래블 때 함께 파괴된다.
	 *   2) MoviePlayer는 게임 스레드가 막힌 동안 전용 스레드로 Slate를 직접 그린다.
	 * 그래서 같은 위젯 타입을 viewport 오버레이용과 MoviePlayer용으로 각각 하나씩 만들고,
	 * 둘이 같은 FLoadingVisualState를 공유해 하나의 연속된 연출처럼 보이게 한다.
	 */
	class SMD_LoadingScreenSlate final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMD_LoadingScreenSlate) {}
			SLATE_ARGUMENT(TSharedPtr<FLoadingVisualState>, VisualState)
			SLATE_ARGUMENT(UTexture2D*, BubbleTexture)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			VisualState = InArgs._VisualState;
			BubbleBrush.SetResourceObject(InArgs._BubbleTexture);
			BubbleBrush.ImageSize = FVector2D(32.0f, 32.0f);
			BubbleBrush.DrawAs = ESlateBrushDrawType::Image;

			TitleFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 24);
			TitleFont.LetterSpacing = 240;
			SubtitleFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14);
			SubtitleFont.LetterSpacing = 80;

			// 이 위젯은 상태를 갖지 않고 매 프레임 경과 시간만 보고 다시 그린다.
			// Tick 대신 active timer로 무효화를 걸어야 게임 스레드가 막힌 동안에도
			// MoviePlayer의 로딩 스레드가 계속 repaint를 수행한다.
			SetCanTick(false);
			RegisterActiveTimer(
				0.0f,
				FWidgetActiveTimerDelegate::CreateSP(this, &SMD_LoadingScreenSlate::RefreshPaint));
		}

		// 화면 전체를 덮는 오버레이이므로 실제 배치 크기는 부모가 정한다.
		// 여기서는 디자인 기준 크기만 알려주고, 실제 스케일은 OnPaint에서 계산한다.
		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return FVector2D(1920.0f, 1080.0f);
		}

		virtual int32 OnPaint(
			const FPaintArgs& Args,
			const FGeometry& AllottedGeometry,
			const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements,
			int32 LayerId,
			const FWidgetStyle& InWidgetStyle,
			bool bParentEnabled) const override
		{
			if (!VisualState.IsValid())
			{
				return LayerId;
			}

			// Time은 로딩 시작부터의 wall-clock 경과 시간, OverallOpacity는 outro fade-out이다.
			// 인트로는 두 단계로 나뉜다. 0.00~0.62초에 배경이 차오르고(FadeInOpacity),
			// 0.56~0.84초에 루프 물방울과 텍스트가 뒤이어 등장한다(TextIntroOpacity).
			// 배경이 먼저 화면을 덮은 뒤 정보가 나타나야 트래블 순간이 가려진다.
			const float Time = static_cast<float>(VisualState->GetElapsedSeconds());
			const float OverallOpacity = VisualState->GetOverallOpacity();
			const float FadeInOpacity = SmoothRange(
				Time,
				0.0f,
				0.62f);
			const float TextIntroOpacity = SmoothRange(Time, 0.56f, 0.84f);

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(),
				FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),
				ESlateDrawEffect::None,
				FLinearColor(0.006f, 0.055f, 0.105f, FadeInOpacity * OverallOpacity));

			DrawIntroBubble(
				AllottedGeometry,
				OutDrawElements,
				LayerId + 1,
				Time,
				FadeInOpacity * OverallOpacity);

			// 완료 표시는 독립된 성공 상태다. 이 시점부터는 진행 중을 암시하는
			// 상승 물방울을 즉시 숨기고 체크와 완료 타이포만 남긴다.
			const float LoopBubbleOpacity = VisualState->IsCompletionVisible()
				? 0.0f
				: TextIntroOpacity * OverallOpacity;
			DrawLoopBubbles(
				AllottedGeometry,
				OutDrawElements,
				LayerId + 2,
				Time,
				LoopBubbleOpacity);

			// 완료 연출은 "기존 문구가 위로 빠지고(LoadingExit) 완료 문구가 아래에서
			// 올라오며 안착(CompletionEntrance)"하는 교차 전환이다.
			// CompletionPulse는 안착 순간에만 살짝 커졌다 돌아오는 scale 보정값이다.
			const float TextOpacity = TextIntroOpacity * OverallOpacity;
			const float CompletionElapsed = VisualState->GetCompletionElapsedSeconds();
			const float LoadingExit = SmoothRange(CompletionElapsed, 0.0f, 0.18f);
			const float CompletionEntrance = SmoothRange(CompletionElapsed, 0.12f, 0.38f);
			const float CompletionPulse = FMath::Sin(
				FMath::Clamp(CompletionElapsed / 0.48f, 0.0f, 1.0f) * PI) * 0.10f;
			DrawCenteredText(
				AllottedGeometry,
				OutDrawElements,
				LayerId + 3,
				LOCTEXT("LoadingTitle", "LOADING"),
				TitleFont,
				0.81f,
				FLinearColor(0.90f, 0.99f, 1.0f, TextOpacity * (1.0f - LoadingExit)),
				1.0f,
				FVector2D(0.0f, -18.0f * LoadingExit));
			DrawCenteredText(
				AllottedGeometry,
				OutDrawElements,
				LayerId + 3,
				VisualState->Subtitle,
				SubtitleFont,
				0.86f,
				FLinearColor(0.65f, 0.90f, 1.0f, 0.95f * TextOpacity * (1.0f - LoadingExit)),
				1.0f,
				FVector2D(0.0f, -12.0f * LoadingExit));
			DrawCompletionCheck(
				AllottedGeometry,
				OutDrawElements,
				LayerId + 3,
				CompletionEntrance,
				TextOpacity);
			DrawCenteredText(
				AllottedGeometry,
				OutDrawElements,
				LayerId + 3,
				LOCTEXT("CompleteTitle", "COMPLETE"),
				TitleFont,
				0.81f,
				FLinearColor(0.62f, 1.0f, 0.88f, TextOpacity * CompletionEntrance),
				0.86f + CompletionEntrance * 0.14f + CompletionPulse,
				FVector2D(0.0f, 18.0f * (1.0f - CompletionEntrance)));
			DrawCenteredText(
				AllottedGeometry,
				OutDrawElements,
				LayerId + 3,
				VisualState->CompletionSubtitle,
				SubtitleFont,
				0.86f,
				FLinearColor(0.70f, 1.0f, 0.90f, 0.98f * TextOpacity * CompletionEntrance),
				1.0f,
				FVector2D(0.0f, 12.0f * (1.0f - CompletionEntrance)));

			return LayerId + 3;
		}

	private:
		void DrawBubble(
			const FGeometry& Geometry,
			FSlateWindowElementList& DrawElements,
			int32 LayerId,
			const FVector2D& Anchor,
			const FVector2D& Translation,
			const FVector2D& BaseSize,
			float Scale,
			float Opacity) const
		{
			if (Opacity <= KINDA_SMALL_NUMBER)
			{
				return;
			}

			// 실제 크기에 맞춰 균일 스케일한 뒤 남는 여백만큼 중앙으로 민다.
			// 종횡비가 달라도 디자인 기준 좌표가 항상 같은 위치를 가리키게 하는 처리이며,
			// viewport와 MoviePlayer가 같은 계산을 쓰므로 트래블 때 위치가 튀지 않는다.
			const FVector2D LocalSize = Geometry.GetLocalSize();
			const float DesignScale = FMath::Max(
				0.01f,
				FMath::Min(LocalSize.X / DesignSize.X, LocalSize.Y / DesignSize.Y));
			const FVector2D DesignOrigin = (LocalSize - DesignSize * DesignScale) * 0.5f;
			const FVector2D DrawSize = BaseSize * Scale * DesignScale;
			const FVector2D DrawPosition = DesignOrigin
				+ DesignSize * Anchor * DesignScale
				+ Translation * DesignScale
				- DrawSize * 0.5f;
			FSlateDrawElement::MakeBox(
				DrawElements,
				LayerId,
				Geometry.ToPaintGeometry(DrawSize, FSlateLayoutTransform(DrawPosition)),
				&BubbleBrush,
				ESlateDrawEffect::None,
				FLinearColor(0.82f, 0.98f, 1.0f, 0.96f * Opacity));
		}

		void DrawLoopBubbles(
			const FGeometry& Geometry,
			FSlateWindowElementList& DrawElements,
			int32 LayerId,
			float Time,
			float OverallOpacity) const
		{
			// 실제 로딩이 얼마나 걸리든 이 구간만 무한 반복한다.
			// +0.55초는 인트로가 끝나는 시점과 루프의 물방울 등장 타이밍을 맞추기 위한 오프셋이다.
			const float LoopTime = FMath::Fmod(
				Time + 0.55f,
				static_cast<float>(LoadingLoopDurationSeconds));
			for (const FBubbleMotion& Bubble : LoopBubbles)
			{
				const float FadeIn = SmoothRange(LoopTime, Bubble.FadeInStart, Bubble.FadeInEnd);
				const float FadeOut = 1.0f - SmoothRange(LoopTime, Bubble.FadeInEnd, Bubble.FadeOutEnd);
				float RiseY = 0.0f;
				if (LoopTime <= Bubble.RiseMidTime)
				{
					RiseY = FMath::Lerp(0.0f, Bubble.RiseMidY,
						SmoothRange(LoopTime, Bubble.FadeInStart, Bubble.RiseMidTime));
				}
				else
				{
					RiseY = FMath::Lerp(Bubble.RiseMidY, Bubble.RiseEndY,
						SmoothRange(LoopTime, Bubble.RiseMidTime, Bubble.RiseEndTime));
				}

				DrawBubble(
					Geometry,
					DrawElements,
					LayerId,
					FVector2D(0.5f, 0.5f),
					FVector2D(Bubble.Position.X, Bubble.Position.Y + RiseY),
					Bubble.Size,
					1.0f,
					FadeIn * FadeOut * OverallOpacity);
			}
		}

		void DrawIntroBubble(
			const FGeometry& Geometry,
			FSlateWindowElementList& DrawElements,
			int32 LayerId,
			float Time,
			float OverallOpacity) const
		{
			// 기존 전환처럼 하나의 물방울이 빠르게 커진 뒤, 반복 물방울로 자연스럽게 넘긴다.
			const float Grow = SmoothRange(Time, 0.02f, 0.54f);
			const float FadeAway = 1.0f - SmoothRange(Time, 0.40f, 0.60f);
			const float Scale = FMath::Lerp(0.30f, 18.0f, Grow);
			DrawBubble(
				Geometry,
				DrawElements,
				LayerId,
				FVector2D(0.5f, 0.5f),
				FVector2D::ZeroVector,
				FVector2D(64.0f, 64.0f),
				Scale,
				0.92f * FadeAway * OverallOpacity);
		}

		void DrawCompletionCheck(
			const FGeometry& Geometry,
			FSlateWindowElementList& DrawElements,
			int32 LayerId,
			float Progress,
			float OverallOpacity) const
		{
			if (Progress <= KINDA_SMALL_NUMBER || OverallOpacity <= KINDA_SMALL_NUMBER)
			{
				return;
			}

			const FVector2D LocalSize = Geometry.GetLocalSize();
			const float DesignScale = FMath::Max(
				0.01f,
				FMath::Min(LocalSize.X / DesignSize.X, LocalSize.Y / DesignSize.Y));
			const FVector2D DesignOrigin = (LocalSize - DesignSize * DesignScale) * 0.5f;
			const FVector2D Center = DesignOrigin + FVector2D(DesignSize.X * 0.5f, DesignSize.Y * 0.745f) * DesignScale;
			const FVector2D Start = Center + FVector2D(-15.0f, 0.0f) * DesignScale;
			const FVector2D Middle = Center + FVector2D(-4.0f, 11.0f) * DesignScale;
			const FVector2D End = Center + FVector2D(20.0f, -15.0f) * DesignScale;
			const FLinearColor Color(0.66f, 1.0f, 0.88f, OverallOpacity * Progress);

			// 체크 표시를 두 획으로 나눠 Progress에 따라 그려 나간다.
			// 0.0~0.5는 첫 획(짧은 내림), 0.5~1.0은 둘째 획(긴 올림)이다.
			TArray<FVector2d> FirstStroke;
			FirstStroke.Add(FVector2d(Start));
			FirstStroke.Add(FVector2d(FMath::Lerp(Start, Middle, FMath::Clamp(Progress * 2.0f, 0.0f, 1.0f))));
			FSlateDrawElement::MakeLines(
				DrawElements,
				LayerId,
				Geometry.ToPaintGeometry(),
				FirstStroke,
				ESlateDrawEffect::None,
				Color,
				true,
				3.0f * DesignScale);

			if (Progress > 0.5f)
			{
				TArray<FVector2d> SecondStroke;
				SecondStroke.Add(FVector2d(Middle));
				SecondStroke.Add(FVector2d(FMath::Lerp(Middle, End, FMath::Clamp((Progress - 0.5f) * 2.0f, 0.0f, 1.0f))));
				FSlateDrawElement::MakeLines(
					DrawElements,
					LayerId,
					Geometry.ToPaintGeometry(),
					SecondStroke,
					ESlateDrawEffect::None,
					Color,
					true,
					3.0f * DesignScale);
			}
		}

		void DrawCenteredText(
			const FGeometry& Geometry,
			FSlateWindowElementList& DrawElements,
			int32 LayerId,
			const FText& Text,
			const FSlateFontInfo& Font,
			float AnchorY,
			const FLinearColor& Color,
			float TextScale = 1.0f,
			const FVector2D& Translation = FVector2D::ZeroVector) const
		{
			if (Color.A <= KINDA_SMALL_NUMBER)
			{
				return;
			}
			const FVector2D LocalSize = Geometry.GetLocalSize();
			const float DesignScale = FMath::Max(
				0.01f,
				FMath::Min(LocalSize.X / DesignSize.X, LocalSize.Y / DesignSize.Y));
			const FVector2D DesignOrigin = (LocalSize - DesignSize * DesignScale) * 0.5f;
			FSlateFontInfo ScaledFont = Font;
			ScaledFont.Size = FMath::Max(1, FMath::RoundToInt(static_cast<float>(Font.Size) * DesignScale * TextScale));
			const TSharedRef<FSlateFontMeasure> FontMeasure =
				FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			const FVector2D TextSize = FontMeasure->Measure(Text, ScaledFont);
			const FVector2D Position(
				DesignOrigin.X + (DesignSize.X * DesignScale - TextSize.X) * 0.5f,
				DesignOrigin.Y + DesignSize.Y * AnchorY * DesignScale - TextSize.Y * 0.5f + Translation.Y * DesignScale);
			FSlateDrawElement::MakeText(
				DrawElements,
				LayerId,
				Geometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Position)),
				Text,
				ScaledFont,
				ESlateDrawEffect::None,
				Color);
		}

		// 위젯이 살아 있는 동안 계속 repaint를 요청한다. 연출 값은 전부 경과 시간에서
		// 파생되므로 별도 상태 갱신 없이 무효화만 걸면 된다.
		EActiveTimerReturnType RefreshPaint(double, float)
		{
			Invalidate(EInvalidateWidgetReason::Paint);
			return EActiveTimerReturnType::Continue;
		}

		TSharedPtr<FLoadingVisualState> VisualState;
		FSlateBrush BubbleBrush;
		FSlateFontInfo TitleFont;
		FSlateFontInfo SubtitleFont;
	};
}

void UMD_LoadingScreenSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 세션 관리자가 먼저 초기화돼 있어야 아래에서 완료 델리게이트를 붙일 수 있다.
	Collection.InitializeDependency<UMD_MultiplayManager>();

	// 세션 작업이 실패하면 트래블이 일어나지 않으므로 로딩 화면을 닫아줘야 한다.
	// 실패 시 화면이 그대로 남아 유저가 갇히는 것을 막는 연결이다.
	if (UMD_MultiplayManager* SessionManager = GetGameInstance()->GetSubsystem<UMD_MultiplayManager>())
	{
		SessionManager->OnCreateSessionComplete.AddDynamic(this, &ThisClass::HandleSessionOperationComplete);
		SessionManager->OnStartGameComplete.AddDynamic(this, &ThisClass::HandleSessionOperationComplete);
		SessionManager->OnEndGameComplete.AddDynamic(this, &ThisClass::HandleSessionOperationComplete);
		SessionManager->OnJoinSessionComplete.AddDynamic(this, &ThisClass::HandleSessionOperationComplete);
		SessionManager->OnDestroySessionComplete.AddDynamic(this, &ThisClass::HandleSessionOperationComplete);
	}

	// 맵 전환의 시작/끝을 잡는 지점. 세션 절차를 거치지 않는 직접 OpenLevel도
	// 여기로 들어오므로, 로딩이 시작되지 않은 상태의 트래블도 화면을 덮을 수 있다.
	FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this, &ThisClass::HandlePreLoadMap);
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::HandlePostLoadMap);
}

void UMD_LoadingScreenSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PreLoadMapWithContext.RemoveAll(this);
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);

	if (UMD_MultiplayManager* SessionManager = GetGameInstance()->GetSubsystem<UMD_MultiplayManager>())
	{
		SessionManager->OnCreateSessionComplete.RemoveDynamic(this, &ThisClass::HandleSessionOperationComplete);
		SessionManager->OnStartGameComplete.RemoveDynamic(this, &ThisClass::HandleSessionOperationComplete);
		SessionManager->OnEndGameComplete.RemoveDynamic(this, &ThisClass::HandleSessionOperationComplete);
		SessionManager->OnJoinSessionComplete.RemoveDynamic(this, &ThisClass::HandleSessionOperationComplete);
		SessionManager->OnDestroySessionComplete.RemoveDynamic(this, &ThisClass::HandleSessionOperationComplete);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(IntroTimerHandle);
		World->GetTimerManager().ClearTimer(WorldReadyTimerHandle);
		World->GetTimerManager().ClearTimer(CompletionTimerHandle);
		World->GetTimerManager().ClearTimer(OutroTimerHandle);
	}

	PendingIntroCallbacks.Reset();
	RemoveLoadingWidgets();
	Super::Deinitialize();
}

void UMD_LoadingScreenSubsystem::BeginLoadingScreen(const FText& Subtitle)
{
	// 데디케이티드 서버에는 그릴 화면이 없다. 각 클라이언트는 자기 쪽에서
	// ClientPrepareLoadingTransition RPC를 받아 스스로 로딩을 시작한다.
	UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// 이미 로딩 중이면 연출을 처음부터 다시 시작하지 않는다.
	// (서버 로컬 호출과 자기 자신에게 온 Client RPC가 겹치는 경우가 있다.)
	if (bLoadingRequested)
	{
		return;
	}
	if (bOutroPlaying)
	{
		RemoveLoadingWidgets();
	}

	CurrentSubtitle = Subtitle.IsEmpty() ? LOCTEXT("DefaultSubtitle", "Preparing expedition...") : Subtitle;
	bLoadingRequested = true;
	bIntroComplete = false;
	bCompletionShowing = false;
	bOutroPlaying = false;
	bFinishRequested = false;
	bFinishWithCompletionRequested = true;
	ReadyStablePollCount = 0;
	ReadinessWaitStartSeconds = 0.0;
	VisualState = MakeShared<MD_LoadingScreenPrivate::FLoadingVisualState>(
		CurrentSubtitle,
		MD_LoadingScreenPrivate::MakeCompletionSubtitle(CurrentSubtitle));
	EnsureViewportLoadingWidget();

	if (World)
	{
		World->GetTimerManager().SetTimer(
			IntroTimerHandle,
			this,
			&ThisClass::HandleIntroAnimationFinished,
			static_cast<float>(MD_LoadingScreenPrivate::IntroDurationSeconds),
			false);
	}
	else
	{
		HandleIntroAnimationFinished();
	}
}

void UMD_LoadingScreenSubsystem::RunAfterIntro(TFunction<void()>&& Callback)
{
	if (!Callback)
	{
		return;
	}
	// 로딩이 시작되지 않았거나 이미 화면이 덮인 뒤라면 기다릴 이유가 없다.
	// 이 즉시 실행 경로가 있어야 로딩 화면을 안 쓰는 호출부에서도 트래블이 멈추지 않는다.
	if (!bLoadingRequested || bIntroComplete)
	{
		Callback();
		return;
	}
	PendingIntroCallbacks.Add(MoveTemp(Callback));
}

void UMD_LoadingScreenSubsystem::CancelLoadingScreen()
{
	FinishLoadingScreen(false);
}

void UMD_LoadingScreenSubsystem::NotifyPlayerReady(APlayerController* PlayerController)
{
	if (bLoadingRequested && IsValid(PlayerController) && PlayerController->IsLocalController())
	{
		TryFinishAfterWorldLoad();
		if (bLoadingRequested)
		{
			if (UWorld* World = GetWorld();
				World && !World->GetTimerManager().IsTimerActive(WorldReadyTimerHandle))
			{
				// 이미 폴링 중이면 마감 시한을 다시 늘리지 않는다.
				StartWorldReadyPolling(World);
			}
		}
	}
}

void UMD_LoadingScreenSubsystem::HandlePreLoadMap(const FWorldContext& WorldContext, const FString& MapName)
{
	if (WorldContext.OwningGameInstance != GetGameInstance())
	{
		return;
	}

	// 세션 절차를 거치지 않은 직접 트래블(예: BP의 OpenLevel)을 위한 fallback이다.
	// 이 시점엔 이미 맵 로딩이 시작돼 인트로를 재생할 시간이 없으므로,
	// bIntroComplete을 곧바로 true로 두어 대기 없이 화면만 덮는다.
	if (!bLoadingRequested)
	{
		CurrentSubtitle = LOCTEXT("DefaultSubtitle", "Preparing expedition...");
		bLoadingRequested = true;
		bIntroComplete = true;
		bCompletionShowing = false;
		bOutroPlaying = false;
		bFinishRequested = false;
		bFinishWithCompletionRequested = true;
		ReadyStablePollCount = 0;
		VisualState = MakeShared<MD_LoadingScreenPrivate::FLoadingVisualState>(
			CurrentSubtitle,
			MD_LoadingScreenPrivate::MakeCompletionSubtitle(CurrentSubtitle));
		EnsureViewportLoadingWidget();
	}

	// UMG widgets added directly to the viewport can survive a hard travel even after
	// their owning world/controller has been destroyed.  The loading layer is pure
	// Slate, so clearing the old world's UMG here cannot remove it and prevents title
	// or pause widgets from being revealed underneath the outro in the destination map.
	if (UWorld* World = GetWorld())
	{
		UWidgetLayoutLibrary::RemoveAllWidgets(World);
	}
	ConfigureMoviePlayer();
}

void UMD_LoadingScreenSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (!bLoadingRequested || !IsValid(LoadedWorld) || LoadedWorld->GetGameInstance() != GetGameInstance())
	{
		return;
	}

	// LoadMap은 GameViewport의 overlay slot을 비울 수 있다. MoviePlayer가 아직 위를
	// 덮고 있는 동안 동일 Slate를 확실히 다시 붙여 자동 종료 뒤 검정 프레임이 없게 한다.
	if (ViewportLoadingWidget.IsValid())
	{
		if (UGameViewportClient* ViewportClient = GetGameInstance()->GetGameViewportClient())
		{
			ViewportClient->RemoveViewportWidgetContent(ViewportLoadingWidget.ToSharedRef());
		}
		bViewportWidgetAttached = false;
	}
	EnsureViewportLoadingWidget();
	ReadyStablePollCount = 0;

	// 타이머는 월드에 종속돼 있어 옛 월드와 함께 사라진다. 인트로가 아직 안 끝났다면
	// 남은 시간만큼 새 월드에서 다시 걸어야 트래블이 인트로를 잡아먹지 않는다.
	if (!bIntroComplete && VisualState.IsValid())
	{
		const float Remaining = FMath::Max(
			0.0f,
			static_cast<float>(MD_LoadingScreenPrivate::IntroDurationSeconds - VisualState->GetElapsedSeconds()));
		LoadedWorld->GetTimerManager().ClearTimer(IntroTimerHandle);
		if (Remaining <= KINDA_SMALL_NUMBER)
		{
			HandleIntroAnimationFinished();
		}
		else
		{
			LoadedWorld->GetTimerManager().SetTimer(
				IntroTimerHandle,
				this,
				&ThisClass::HandleIntroAnimationFinished,
				Remaining,
				false);
		}
	}

	// 새 월드가 올라왔으므로 준비 판정과 타임아웃 시한을 여기서부터 새로 센다.
	StartWorldReadyPolling(LoadedWorld);
}

void UMD_LoadingScreenSubsystem::StartWorldReadyPolling(UWorld* TimerWorld)
{
	if (!IsValid(TimerWorld))
	{
		return;
	}

	ReadyStablePollCount = 0;
	ReadinessWaitStartSeconds = FPlatformTime::Seconds();
	TimerWorld->GetTimerManager().ClearTimer(WorldReadyTimerHandle);
	TimerWorld->GetTimerManager().SetTimer(
		WorldReadyTimerHandle,
		this,
		&ThisClass::TryFinishAfterWorldLoad,
		MD_LoadingScreenPrivate::ReadinessPollSeconds,
		true);
}

/**
 * 새 월드가 화면을 보여줘도 될 만큼 준비됐는지 검사한다.
 * "맵 파일을 다 읽음"과 "플레이 가능"은 다르다. 여기서 걸러내지 않으면 텍스처가
 * 체크무늬로 보이거나, 서브레벨이 안 떠 바닥이 뚫리거나, 조작이 안 되는 상태로 진입한다.
 *
 * OutBlockingReason이 nullptr이면 사유 문자열을 만들지 않는다. 이 함수는 초당 20회
 * 호출되므로 평상시에는 FString 할당을 피하고, 타임아웃이 났을 때만 한 번 사유를 만든다.
 */
bool UMD_LoadingScreenSubsystem::IsWorldReadyToFinish(FString* OutBlockingReason) const
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		if (OutBlockingReason) { *OutBlockingReason = TEXT("World가 유효하지 않음"); }
		return false;
	}
	if (!World->HasBegunPlay())
	{
		if (OutBlockingReason) { *OutBlockingReason = TEXT("World가 아직 BeginPlay를 시작하지 않음"); }
		return false;
	}
	if (!IsValid(GetGameInstance()->GetFirstLocalPlayerController(World)))
	{
		if (OutBlockingReason) { *OutBlockingReason = TEXT("로컬 PlayerController가 아직 생성되지 않음"); }
		return false;
	}
	if (IsAsyncLoading())
	{
		if (OutBlockingReason) { *OutBlockingReason = TEXT("비동기 로딩이 진행 중"); }
		return false;
	}
	if (World->IsVisibilityRequestPending())
	{
		if (OutBlockingReason) { *OutBlockingReason = TEXT("레벨 visibility 요청이 처리 대기 중"); }
		return false;
	}

	bool bStreamingBlocked = false;
	TArray<FString> BlockingLevels;
	for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
	{
		if (!IsValid(StreamingLevel))
		{
			continue;
		}
		const bool bWaitingLoad = StreamingLevel->ShouldBeLoaded() && !StreamingLevel->IsLevelLoaded();
		const bool bWaitingVisible = StreamingLevel->ShouldBeVisible() && !StreamingLevel->IsLevelVisible();
		if (!bWaitingLoad && !bWaitingVisible)
		{
			continue;
		}

		bStreamingBlocked = true;
		if (!OutBlockingReason)
		{
			// 빠른 경로에서는 첫 번째 미준비 레벨만 확인하면 충분하다.
			return false;
		}
		BlockingLevels.Add(FString::Printf(
			TEXT("%s(%s)"),
			*StreamingLevel->GetWorldAssetPackageName(),
			bWaitingLoad ? TEXT("미로드") : TEXT("미표시")));
	}
	if (bStreamingBlocked)
	{
		if (OutBlockingReason)
		{
			*OutBlockingReason = FString::Printf(
				TEXT("streaming level 대기: %s"), *FString::Join(BlockingLevels, TEXT(", ")));
		}
		return false;
	}

	// Audio streaming cache에는 절대 간섭하지 않는다. Texture/mesh manager의
	// 공개 상태만 읽고, 원하는 render asset이 없는 상태가 이어질 때만 fade한다.
	// IStreamingManager::BlockTillAllRequestsFinished를 부르면 AudioStreamingCache의
	// 종료 Assert(NumberOfLoadsInFlight)가 터지므로 그 API는 쓰면 안 된다.
	const IRenderAssetStreamingManager& RenderAssetStreaming =
		IStreamingManager::Get().GetRenderAssetStreamingManager();
	if (const int32 WantingResources = RenderAssetStreaming.GetNumWantingResources(); WantingResources > 0)
	{
		if (OutBlockingReason)
		{
			*OutBlockingReason = FString::Printf(
				TEXT("render asset streaming 대기 %d건"), WantingResources);
		}
		return false;
	}

	return true;
}

void UMD_LoadingScreenSubsystem::TryFinishAfterWorldLoad()
{
	if (!bLoadingRequested)
	{
		return;
	}

	if (!IsWorldReadyToFinish(nullptr))
	{
		ReadyStablePollCount = 0;

		// 준비 조건이 영영 충족되지 않는 맵/에셋 구성에서도 로딩 화면이
		// 남지 않도록, 상한을 넘기면 사유를 남기고 강제로 닫는다.
		const double ElapsedSeconds = ReadinessWaitStartSeconds > 0.0
			? FPlatformTime::Seconds() - ReadinessWaitStartSeconds
			: 0.0;
		if (ElapsedSeconds > MD_LoadingScreenPrivate::ReadinessTimeoutSeconds)
		{
			FString BlockingReason;
			IsWorldReadyToFinish(&BlockingReason);
			UE_LOG(MarineDemolition, Warning,
				TEXT("%s 로딩 준비 판정이 %.1f초를 초과해 강제 종료합니다. 원인: %s"),
				*UE_CALL_LOCATIONINFO,
				ElapsedSeconds,
				*BlockingReason);

			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().ClearTimer(WorldReadyTimerHandle);
			}
			ReadinessWaitStartSeconds = 0.0;
			// 실제로는 준비되지 않았으므로 COMPLETE 연출 없이 곧바로 fade-out한다.
			FinishLoadingScreen(false);
		}
		return;
	}

	++ReadyStablePollCount;
	if (ReadyStablePollCount < MD_LoadingScreenPrivate::RequiredReadyStablePolls)
	{
		return;
	}

	ReadinessWaitStartSeconds = 0.0;
	GetWorld()->GetTimerManager().ClearTimer(WorldReadyTimerHandle);
	FinishLoadingScreen();
}

void UMD_LoadingScreenSubsystem::FinishLoadingScreen(bool bShowCompletion)
{
	if (!bLoadingRequested || bOutroPlaying || bCompletionShowing)
	{
		return;
	}
	// 인트로가 끝나기 전에 준비가 완료될 수 있다(맵이 아주 가벼운 경우).
	// 그때 바로 닫으면 화면이 덮였다가 즉시 걷히며 깜빡이므로,
	// 요청만 기록해 두고 HandleIntroAnimationFinished에서 이어서 처리한다.
	if (!bIntroComplete)
	{
		bFinishRequested = true;
		bFinishWithCompletionRequested = bShowCompletion;
		return;
	}

	bFinishRequested = false;
	bFinishWithCompletionRequested = true;
	ReadyStablePollCount = 0;
	ReadinessWaitStartSeconds = 0.0;
	PendingIntroCallbacks.Reset();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WorldReadyTimerHandle);
	}
	EnsureViewportLoadingWidget();

	// bShowCompletion이 false인 경우는 정상 완료가 아니다(세션 실패, 준비 판정 타임아웃).
	// 준비되지 않았는데 COMPLETE/체크를 띄우면 거짓 신호가 되므로 곧바로 outro만 재생한다.
	if (bShowCompletion && VisualState.IsValid())
	{
		bCompletionShowing = true;
		VisualState->BeginCompletion();
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				CompletionTimerHandle,
				this,
				&ThisClass::HandleCompletionDisplayFinished,
				static_cast<float>(MD_LoadingScreenPrivate::CompletionDisplaySeconds),
				false);
			return;
		}
	}

	BeginOutro();
}

void UMD_LoadingScreenSubsystem::HandleCompletionDisplayFinished()
{
	bCompletionShowing = false;
	BeginOutro();
}

void UMD_LoadingScreenSubsystem::BeginOutro()
{
	if (!bLoadingRequested || bOutroPlaying)
	{
		return;
	}

	bLoadingRequested = false;
	bOutroPlaying = true;
	bFinishRequested = false;
	ReadyStablePollCount = 0;
	ReadinessWaitStartSeconds = 0.0;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CompletionTimerHandle);
	}
	if (VisualState.IsValid())
	{
		VisualState->BeginOutro();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			OutroTimerHandle,
			this,
			&ThisClass::HandleOutroAnimationFinished,
			static_cast<float>(MD_LoadingScreenPrivate::OutroDurationSeconds),
			false);
	}
	else
	{
		HandleOutroAnimationFinished();
	}
}

// viewport 오버레이 위젯을 만들고 붙인다. 위젯 자체는 재사용하고 부착 여부만 따로
// 추적하는데, 하드 트래블 후 GameViewport의 overlay slot이 비워져 같은 위젯을
// 다시 붙여야 하는 경우가 있기 때문이다(HandlePostLoadMap 참고).
void UMD_LoadingScreenSubsystem::EnsureViewportLoadingWidget()
{
	if (!VisualState.IsValid())
	{
		return;
	}
	if (!BubbleTexture)
	{
		BubbleTexture = LoadObject<UTexture2D>(nullptr, MD_LoadingScreenPrivate::BubbleTexturePath);
	}
	if (!ViewportLoadingWidget.IsValid())
	{
		ViewportLoadingWidget = SNew(MD_LoadingScreenPrivate::SMD_LoadingScreenSlate)
			.VisualState(VisualState)
			.BubbleTexture(BubbleTexture);
	}

	if (!bViewportWidgetAttached)
	{
		if (UGameViewportClient* ViewportClient = GetGameInstance()->GetGameViewportClient())
		{
			ViewportClient->AddViewportWidgetContent(
				ViewportLoadingWidget.ToSharedRef(),
				MD_LoadingScreenPrivate::ViewportZOrder);
			bViewportWidgetAttached = true;
		}
	}
}

void UMD_LoadingScreenSubsystem::ConfigureMoviePlayer()
{
	if (!VisualState.IsValid() || !IsMoviePlayerEnabled() || !GetMoviePlayer()->IsInitialized())
	{
		return;
	}
	if (!BubbleTexture)
	{
		BubbleTexture = LoadObject<UTexture2D>(nullptr, MD_LoadingScreenPrivate::BubbleTexturePath);
	}

	// viewport용과 별개의 위젯 인스턴스지만 VisualState는 같은 것을 넘긴다.
	// 그래서 화면을 그리는 주체가 바뀌어도 연출은 같은 시간축 위에서 이어진다.
	MoviePlayerLoadingWidget = SNew(MD_LoadingScreenPrivate::SMD_LoadingScreenSlate)
		.VisualState(VisualState)
		.BubbleTexture(BubbleTexture);

	FLoadingScreenAttributes Attributes;
	// -1: 최소 표시 시간을 강제하지 않는다. 표시 길이는 우리 준비 판정이 결정한다.
	Attributes.MinimumLoadingScreenDisplayTime = -1.0f;
	// 맵 로딩이 끝나면 MoviePlayer는 스스로 물러나고, 이후는 viewport 오버레이가 이어받는다.
	Attributes.bAutoCompleteWhenLoadingCompletes = true;
	Attributes.bMoviesAreSkippable = false;
	Attributes.bWaitForManualStop = false;
	Attributes.bAllowInEarlyStartup = false;
	// false여야 MoviePlayer가 전용 Slate 스레드를 띄워 게임 스레드가 막힌 동안에도 그린다.
	// true로 바꾸면 트래블 전 PlayMovie와 겹쳐 RayTracingGeometryManager 중복 Tick Assert가 난다.
	Attributes.bAllowEngineTick = false;
	Attributes.WidgetLoadingScreen = MoviePlayerLoadingWidget.ToSharedRef();
	GetMoviePlayer()->SetupLoadingScreen(Attributes);
}

void UMD_LoadingScreenSubsystem::FlushPendingIntroCallbacks()
{
	// 콜백 안에서 트래블이 시작되며 다시 로딩 상태를 건드릴 수 있으므로,
	// 원본을 먼저 비워 두고 지역 복사본을 순회한다.
	TArray<TFunction<void()>> Callbacks = MoveTemp(PendingIntroCallbacks);
	PendingIntroCallbacks.Reset();
	for (TFunction<void()>& Callback : Callbacks)
	{
		if (Callback)
		{
			Callback();
		}
	}
}

// 연출이 완전히 끝난 뒤(또는 서브시스템 종료 시) 모든 상태를 초기값으로 되돌린다.
// 다음 로딩이 이전 상태를 물려받지 않도록 위젯 참조와 플래그를 한 곳에서 정리한다.
void UMD_LoadingScreenSubsystem::RemoveLoadingWidgets()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(IntroTimerHandle);
		World->GetTimerManager().ClearTimer(WorldReadyTimerHandle);
		World->GetTimerManager().ClearTimer(CompletionTimerHandle);
		World->GetTimerManager().ClearTimer(OutroTimerHandle);
	}

	if (ViewportLoadingWidget.IsValid())
	{
		if (UGameViewportClient* ViewportClient = GetGameInstance()->GetGameViewportClient())
		{
			ViewportClient->RemoveViewportWidgetContent(ViewportLoadingWidget.ToSharedRef());
		}
	}

	ViewportLoadingWidget.Reset();
	MoviePlayerLoadingWidget.Reset();
	VisualState.Reset();
	bViewportWidgetAttached = false;
	bLoadingRequested = false;
	bIntroComplete = false;
	bCompletionShowing = false;
	bOutroPlaying = false;
	bFinishRequested = false;
	bFinishWithCompletionRequested = true;
	ReadyStablePollCount = 0;
}

void UMD_LoadingScreenSubsystem::HandleIntroAnimationFinished()
{
	bIntroComplete = true;
	if (bFinishRequested)
	{
		FinishLoadingScreen(bFinishWithCompletionRequested);
		return;
	}
	FlushPendingIntroCallbacks();
}

void UMD_LoadingScreenSubsystem::HandleOutroAnimationFinished()
{
	RemoveLoadingWidgets();
}

// 세션 작업이 실패하면 트래블이 일어나지 않는다. 그대로 두면 준비 판정이 영영
// 시작되지 않아 화면이 남으므로, COMPLETE 없이 즉시 걷어낸다.
// 성공한 경우에는 트래블이 이어지므로 여기서 아무것도 하지 않는다.
void UMD_LoadingScreenSubsystem::HandleSessionOperationComplete(bool bWasSuccessful)
{
	if (!bWasSuccessful)
	{
		CancelLoadingScreen();
	}
}

#undef LOCTEXT_NAMESPACE

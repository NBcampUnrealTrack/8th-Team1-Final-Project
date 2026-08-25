#pragma once

#include "CoreMinimal.h"
#include "Layout/Geometry.h"
#include "Templates/Atomic.h"
#include "MD_LoadingScreenTypes.generated.h"

/** 로딩 연출의 진행 단계. 서브시스템은 개별 플래그 대신 이 하나로 상태를 관리한다. */
UENUM()
enum class EMD_LoadingPhase : uint8
{
	/** 연출이 화면에 없다. */
	Inactive,
	/** 화면을 덮는 중. 이 단계에서는 트래블을 시작하지 않는다. */
	Intro,
	/** 화면이 덮인 채 루프. 트래블과 준비 판정이 이 단계에서 일어난다. */
	Loading,
	/** 준비 완료를 알리는 COMPLETE 표시. */
	Completion,
	/** fade-out. 끝나면 위젯과 상태를 모두 되돌린다. */
	Outro
};

/** 인트로가 끝나기 전에 들어온 종료 요청. 인트로 종료 콜백이 소비한다. */
UENUM()
enum class EMD_LoadingFinishRequest : uint8
{
	None,
	/** 정상 완료. COMPLETE 연출을 거쳐 outro로 넘어간다. */
	WithCompletion,
	/** 세션 실패나 준비 판정 타임아웃. COMPLETE 없이 곧바로 outro. */
	WithoutCompletion
};

/** 준비 판정을 막고 있는 원인. 타임아웃 경고 로그에만 쓴다. */
UENUM()
enum class EMD_LoadingReadinessBlocker : uint8
{
	/** 막는 것이 없다. 로딩을 닫아도 된다. */
	None,
	WorldInvalid,
	BeginPlayPending,
	PlayerControllerMissing,
	AsyncLoading,
	LevelVisibilityPending,
	StreamingLevelPending,
	RenderAssetStreaming
};

namespace MD_LoadingScreen
{
	// 연출 순서: Intro 0.90초(화면을 완전히 덮음) → Loading loop 2.15초 무한 반복
	// → 준비 완료 시 Completion 0.85초 → Outro 1.10초 fade-out.
	constexpr double IntroDurationSeconds = 0.90;
	constexpr double CompletionDisplaySeconds = 0.85;
	constexpr double LoadingLoopDurationSeconds = 2.15;
	constexpr double OutroDurationSeconds = 1.10;

	// 0.05초 x 10회 = 약 0.5초간 조건이 유지돼야 닫는다.
	// 한 프레임만 만족했다가 곧바로 다시 로딩이 시작되는 경우를 거르기 위한 장치다.
	constexpr float ReadinessPollSeconds = 0.05f;
	constexpr int32 RequiredReadyStablePolls = 10;
	// 스트리밍이 영영 해소되지 않을 때 로딩 화면이 무한히 남는 것을 막는 상한.
	constexpr double ReadinessTimeoutSeconds = 45.0;

	// viewport와 MoviePlayer는 스케일 기준이 달라 트래블 순간 폰트/물방울이 튀었다.
	// 양쪽 모두 이 고정 해상도로 계산한 뒤 실제 크기에 맞춰 균일 스케일한다.
	inline const FVector2D DesignSize(1920.0f, 1080.0f);

	/** 게임플레이 UMG(HUD/메뉴)보다 반드시 위에 그려야 하므로 의도적으로 큰 값이다. */
	constexpr int32 ViewportZOrder = 10000;

	/** 타임아웃 경고에 남길 사유. 평상시에는 호출되지 않는다. */
	inline const TCHAR* ToString(EMD_LoadingReadinessBlocker Blocker)
	{
		switch (Blocker)
		{
		case EMD_LoadingReadinessBlocker::WorldInvalid:            return TEXT("World가 유효하지 않음");
		case EMD_LoadingReadinessBlocker::BeginPlayPending:        return TEXT("World가 아직 BeginPlay를 시작하지 않음");
		case EMD_LoadingReadinessBlocker::PlayerControllerMissing: return TEXT("로컬 PlayerController가 아직 생성되지 않음");
		case EMD_LoadingReadinessBlocker::AsyncLoading:            return TEXT("비동기 로딩이 진행 중");
		case EMD_LoadingReadinessBlocker::LevelVisibilityPending:  return TEXT("레벨 visibility 요청이 처리 대기 중");
		case EMD_LoadingReadinessBlocker::StreamingLevelPending:   return TEXT("streaming level 로드/표시 대기");
		case EMD_LoadingReadinessBlocker::RenderAssetStreaming:    return TEXT("render asset streaming 대기");
		default:                                                   return TEXT("없음");
		}
	}
}

/**
 * viewport 위젯과 MoviePlayer 위젯이 함께 참조하는 단일 연출 상태.
 * 트래블 전후로 그리는 주체가 바뀌어도 같은 시간축을 보게 해 연출이 이어진다.
 *
 * 경과 시간이 world time이 아니라 Cycles64인 이유: 하드 트래블 중에는 게임 스레드가
 * LoadMap 안에서 멈춰 world time이 흐르지 않는다. Completion/Outro 시각이 TAtomic인
 * 이유: 게임 스레드가 쓰고 MoviePlayer 로딩 스레드가 읽는다.
 */
struct FMD_LoadingVisualState
{
	FMD_LoadingVisualState(const FText& InSubtitle, const FText& InCompletionSubtitle)
		: Subtitle(InSubtitle), CompletionSubtitle(InCompletionSubtitle), StartCycles(FPlatformTime::Cycles64()) {}

	double GetElapsedSeconds() const { return SecondsSince(StartCycles); }
	void BeginCompletion() { StampOnce(CompletionStartCycles); }
	void BeginOutro() { StampOnce(OutroStartCycles); }
	bool IsCompletionVisible() const { return CompletionStartCycles.Load() != 0; }

	float GetCompletionElapsedSeconds() const
	{
		const uint64 Stamp = CompletionStartCycles.Load();
		return Stamp == 0 ? 0.0f : static_cast<float>(SecondsSince(Stamp));
	}

	/** outro 진행에 따른 전체 fade-out. outro 전에는 항상 1.0이다. */
	float GetOverallOpacity() const
	{
		const uint64 Stamp = OutroStartCycles.Load();
		if (Stamp == 0) { return 1.0f; }

		const float Alpha = FMath::Clamp(
			static_cast<float>(SecondsSince(Stamp) / MD_LoadingScreen::OutroDurationSeconds), 0.0f, 1.0f);
		return 1.0f - FMath::SmoothStep(0.0f, 1.0f, Alpha);
	}

	FText Subtitle;
	FText CompletionSubtitle;

private:
	static double SecondsSince(uint64 Cycles) { return FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - Cycles); }
	static void StampOnce(TAtomic<uint64>& Target)
	{
		if (Target.Load() == 0) { Target.Store(FPlatformTime::Cycles64()); }
	}

	uint64 StartCycles;
	TAtomic<uint64> CompletionStartCycles{0};
	TAtomic<uint64> OutroStartCycles{0};
};

/** 로딩 루프 물방울 하나의 등장/상승 키값. 시간은 루프 안에서의 초 단위다. */
struct FMD_LoadingBubbleMotion
{
	FVector2D Position, Size;
	float FadeInStart, FadeInEnd, FadeOutEnd;
	float RiseMidTime, RiseMidY, RiseEndTime, RiseEndY;
};

/**
 * 디자인 해상도(MD_LoadingScreen::DesignSize) 좌표를 실제 위젯 좌표로 옮기는 균일 스케일 변환.
 * viewport와 MoviePlayer가 같은 계산을 쓰기 때문에 트래블 순간 위치가 튀지 않는다.
 */
struct FMD_LoadingDesignSpace
{
	explicit FMD_LoadingDesignSpace(const FGeometry& InGeometry)
		: Geometry(InGeometry)
	{
		const FVector2D LocalSize = Geometry.GetLocalSize();
		Scale = FMath::Max(0.01f, FMath::Min(
			LocalSize.X / MD_LoadingScreen::DesignSize.X,
			LocalSize.Y / MD_LoadingScreen::DesignSize.Y));
		// 종횡비가 달라도 디자인 좌표가 같은 위치를 가리키게 남는 여백만큼 중앙으로 민다.
		Origin = (LocalSize - MD_LoadingScreen::DesignSize * Scale) * 0.5f;
	}

	FVector2D Point(const FVector2D& DesignPoint) const { return Origin + DesignPoint * Scale; }

	const FGeometry& Geometry;
	FVector2D Origin = FVector2D::ZeroVector;
	float Scale = 1.0f;
};

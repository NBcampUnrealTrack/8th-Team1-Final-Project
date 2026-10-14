#include "Gameplay/Interaction/MD_InteractionSettings.h"

#include "Engine/DataTable.h"

UMD_InteractionSettings::UMD_InteractionSettings()
{
	// 별도 행동 구분이 없는 기본 상호작용 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::Interact, NSLOCTEXT("MDInteraction", "TypeInteract", "상호작용"));
	// 소지품으로 회수하는 아이템 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::Pickup, NSLOCTEXT("MDInteraction", "TypePickup", "줍기"));
	// 손으로 운반하는 물체 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::Carry, NSLOCTEXT("MDInteraction", "TypeCarry", "들기"));
	// 인물 대화 시작 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::Talk, NSLOCTEXT("MDInteraction", "TypeTalk", "대화하기"));
	// 카트 운반 시작 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::PushCart, NSLOCTEXT("MDInteraction", "TypePushCart", "밀기"));
	// 보급기 쓰레기통 생성 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::SpawnTrashBin, NSLOCTEXT("MDInteraction", "TypeSpawnTrashBin", "쓰레기 통 소환"));
	// 처리장 쓰레기 분류 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::SortTrash, NSLOCTEXT("MDInteraction", "TypeSortTrash", "분류"));
	// 문이나 보관함 개방 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::Open, NSLOCTEXT("MDInteraction", "TypeOpen", "열기"));
	// 퀘스트 물품 회수 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::Collect, NSLOCTEXT("MDInteraction", "TypeCollect", "회수"));
	// 기록물 열람 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::Read, NSLOCTEXT("MDInteraction", "TypeRead", "읽기"));
	// 플레이어 외형 설정 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::Customize, NSLOCTEXT("MDInteraction", "TypeCustomize", "외형 변경"));
	// 하루 작업 마감 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::EndDay, NSLOCTEXT("MDInteraction", "TypeEndDay", "하루 마감"));
	// 목적지 이동 화면 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::Travel, NSLOCTEXT("MDInteraction", "TypeTravel", "이동"));
	// 섹터 작업 완료 제출 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::CloseSector, NSLOCTEXT("MDInteraction", "TypeCloseSector", "구역 마감"));
	// 시설 제어 장치 조작 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::Operate, NSLOCTEXT("MDInteraction", "TypeOperate", "조작"));
	// 고장 시설 복구 시작 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::Repair, NSLOCTEXT("MDInteraction", "TypeRepair", "수리"));
	// 지문 패드 인증 시작 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::Authenticate, NSLOCTEXT("MDInteraction", "TypeAuthenticate", "길게 눌러 지문 인증"));
	// 단말 데이터 덤프 홀드 입력 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::StartDump, NSLOCTEXT("MDInteraction", "TypeStartDump", "길게 눌러 데이터 덤프"));
	// 다인 작업에서 문 잠금을 동반하는 지침 사용 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::UseInstruction, NSLOCTEXT("MDInteraction", "TypeUseInstruction", "지침 사용 · 다인 플레이 시 문 잠금"));
	// 지침 정답을 변경하고 문을 여는 재시작 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::RestartInstruction, NSLOCTEXT("MDInteraction", "TypeRestartInstruction", "정답 변경 · 문 열기"));
	// 차단기 노브를 다음 위치로 이동하는 조작 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::AdvanceBreakerRow, NSLOCTEXT("MDInteraction", "TypeAdvanceBreakerRow", "다음 칸"));
	// 하강 방향으로 엘리베이터를 출발시키는 레버 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::ElevatorDown, NSLOCTEXT("MDInteraction", "TypeElevatorDown", "레버 당기기 (내려가기)"));
	// 상승 방향으로 엘리베이터를 출발시키는 레버 안내
	InteractionPromptTexts.Add(EMD_InteractionPromptType::ElevatorUp, NSLOCTEXT("MDInteraction", "TypeElevatorUp", "레버 당기기 (올라가기)"));
}

const UMD_InteractionSettings* UMD_InteractionSettings::Get()
{
	return GetDefault<UMD_InteractionSettings>();
}

const UDataTable* UMD_InteractionSettings::GetInteractionPresentationTable() const
{
	return InteractionPresentationTable.LoadSynchronous();
}

FText UMD_InteractionSettings::GetPromptText(EMD_InteractionPromptType PromptType) const
{
	// 표시 제외 유형에 대한 행동 안내 생략
	if (PromptType == EMD_InteractionPromptType::None)
	{
		// 문구 설정 여부와 관계없는 명시적 숨김
		return FText::GetEmpty();
	}

	// 읽기 전용 설정 맵에서 선택 유형의 문구 조회
	if (const FText* PromptText = InteractionPromptTexts.Find(PromptType); PromptText != nullptr)
	{
		// 설정에서 수정한 공용 문구 반환
		return *PromptText;
	}

	// 등록되지 않은 유형의 행동 안내 생략
	return FText::GetEmpty();
}

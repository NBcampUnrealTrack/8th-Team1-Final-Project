# V2 SFX 제작·적용 현황

작성일: 2026-10-13

## 제작 방향

짧고 경쾌한 기계·장난감·접촉음을 중심으로 캐주얼 SF 청소 게임의 분위기를 맞췄다. 경고는 또렷하게, 반복 작업음은 낮게 구성했다. 효과음 원본은 모노 48 kHz로 제작하며 수중 필터는 기존 오디오 프레임워크에서 적용한다. 배경음악·환경음은 유지한다.

기존에 승인된 귀여운 피격 반응과 +5반음 피치는 유지한다. V2 SoundWave 74종(승인 피격음 복제 포함)과 기존 경로를 유지한 일반 발소리 5종을 구성했다.

## 적용 범위

- 시설: 문 고장 경고, 스파크, 문 열림·닫힘, 수리 시작·진행·취소·완료, 절연테이프 사용, 펌프 누수, 분리수거기 고장.
- 협동 수리: 지문 요청·스캔·대기·완료·시간초과, 지침 사용·재시작, 차단기 조작 시작·노브 위치 확정·복구.
- 도구·운반: 스캐너 모드·정전 라이트, 헤라 휘두르기·따개비 제거, 절단기, 폼건, 집기·놓기·던지기 충전·투척, 쓰레기통 투입·배출.
- 경제·UI: 분리수거 성공·오류, 구매·판매, 대출·상환·이자, 보고서 열기·결과, 기존 클릭·호버, 핫바 전환.
- 플레이어: 산소 경고·고갈·회복, 위험 심박 반복음, 사망·복귀·발소리.

월드 효과음은 서버에서 확정된 상태와 기존 복제 이벤트에 연결한다. 도구의 예측 재생은 기존 로컬 재생/멀티캐스트 중복 방지 규칙을 유지한다. 투척은 서버 성공 확정 후 사용자에게도 들리도록 별도 확정 재생 경로를 사용한다. 개인 UI·충전·생존 경고는 해당 플레이어에게 재생한다. 반복음은 취소·복구·종료 시 정지한다.

## 연결 대기

빠루 사용·강제 개방, 유리창 그리기·지우기, 계약 완료·게임 오버 전용음은 준비했다. 현재 확인된 실제 게임플레이 호출 지점이 없어 임의로 기능을 추가하지 않았다. 해당 기능 구현 시 아래 태그를 연결해야 한다.

기존 연속 헤라 긁기 태그는 호환용으로 남겼다. 현재 스트로크 방식에는 휘두르기와 실제 얼룩 제거·따개비 제거 성공음을 사용한다.

## 맵 관련 확인

검증 당시 최신 원격 Ocean 맵에는 배선반 액터가 없었다. 수리 효과음은 플레이 중에만 생성한 문·배선반으로 검증하며 맵에 저장하지 않는다. 기존 Misery 세션 배치본을 Persist한 뒤 실제 방 배치를 확인해야 한다.

## 에셋 목록

| 이벤트 | 에셋 | 상태 |
| --- | --- | --- |
| `SFX.Clean.Hera.ScrapeBarnacle` | `/Game/SFX/V2/S_V2_Clean_Hera_ScrapeBarnacle` | 카탈로그 등록 |
| `SFX.Clean.Barnacle.Pop` | `/Game/SFX/V2/S_V2_Clean_Barnacle_Pop` | 카탈로그 등록 |
| `SFX.Clean.Sludge.Drop` | `/Game/SFX/V2/S_V2_Clean_Sludge_Drop` | 카탈로그 등록 |
| `SFX.Cut.Cutter.Start` | `/Game/SFX/V2/S_V2_Cut_Cutter_Start` | 카탈로그 등록 |
| `SFX.Cut.Cutter.Loop` | `/Game/SFX/V2/S_V2_Cut_Cutter_Loop` | 카탈로그 등록 |
| `SFX.Cut.Cutter.Stop` | `/Game/SFX/V2/S_V2_Cut_Cutter_Stop` | 카탈로그 등록 |
| `SFX.Cut.Wall.Break` | `/Game/SFX/V2/S_V2_Cut_Wall_Break` | 카탈로그 등록 |
| `SFX.Hazard.Cable.Cut` | `/Game/SFX/V2/S_V2_Hazard_Cable_Cut` | 카탈로그 등록 |
| `SFX.Hazard.Cable.SparkLoop` | `/Game/SFX/V2/S_V2_Hazard_Cable_SparkLoop` | 카탈로그 등록 |
| `SFX.Hazard.Light.Off` | `/Game/SFX/V2/S_V2_Hazard_Light_Off` | 카탈로그 등록 |
| `SFX.Hazard.Light.On` | `/Game/SFX/V2/S_V2_Hazard_Light_On` | 카탈로그 등록 |
| `SFX.Hazard.FoamGun.Fire` | `/Game/SFX/V2/S_V2_Hazard_FoamGun_Fire` | 카탈로그 등록 |
| `SFX.Hazard.FoamGun.Empty` | `/Game/SFX/V2/S_V2_Hazard_FoamGun_Empty` | 카탈로그 등록 |
| `SFX.Hazard.Pipe.GushLoop` | `/Game/SFX/V2/S_V2_Hazard_Pipe_GushLoop` | 카탈로그 등록 |
| `SFX.Carry.Hand.Pickup` | `/Game/SFX/V2/S_V2_Carry_Hand_Pickup` | 카탈로그 등록 |
| `SFX.Carry.Hand.Drop` | `/Game/SFX/V2/S_V2_Carry_Hand_Drop` | 카탈로그 등록 |
| `SFX.Carry.Hand.ThrowCharge` | `/Game/SFX/V2/S_V2_Carry_Hand_ThrowCharge` | 카탈로그 등록 |
| `SFX.Carry.Hand.Throw` | `/Game/SFX/V2/S_V2_Carry_Hand_Throw` | 카탈로그 등록 |
| `SFX.Carry.Bin.Insert` | `/Game/SFX/V2/S_V2_Carry_Bin_Insert` | 카탈로그 등록 |
| `SFX.Carry.Dispenser.Release` | `/Game/SFX/V2/S_V2_Carry_Dispenser_Release` | 카탈로그 등록 |
| `SFX.Character.Oxygen.Depleted` | `/Game/SFX/V2/S_V2_Character_Oxygen_Depleted` | 카탈로그 등록 |
| `SFX.Character.Oxygen.Low` | `/Game/SFX/V2/S_V2_Character_Oxygen_Low` | 카탈로그 등록 |
| `SFX.Character.Oxygen.Refill` | `/Game/SFX/V2/S_V2_Character_Oxygen_Refill` | 카탈로그 등록 |
| `SFX.Character.Oxygen.HeartbeatLoop` | `/Game/SFX/V2/S_V2_Character_Oxygen_HeartbeatLoop` | 카탈로그 등록 |
| `SFX.Character.Death` | `/Game/SFX/V2/S_V2_Character_Death` | 카탈로그 등록 |
| `SFX.Character.Respawn` | `/Game/SFX/V2/S_V2_Character_Respawn` | 카탈로그 등록 |
| `SFX.Tool.Hotbar.Switch` | `/Game/SFX/V2/S_V2_Tool_Hotbar_Switch` | 카탈로그 등록 |
| `SFX.Tool.Interact.Denied` | `/Game/SFX/V2/S_V2_Tool_Interact_Denied` | 카탈로그 등록 |
| `SFX.Tool.Scanner.PulseLoop` | `/Game/SFX/V2/S_V2_Tool_Scanner_PulseLoop` | 카탈로그 등록 |
| `SFX.Tool.Scanner.ModeSwitch` | `/Game/SFX/V2/S_V2_Tool_Scanner_ModeSwitch` | 카탈로그 등록 |
| `SFX.Tool.Scanner.LightOn` | `/Game/SFX/V2/S_V2_Tool_Scanner_LightOn` | 카탈로그 등록 |
| `SFX.Tool.Scanner.LightOff` | `/Game/SFX/V2/S_V2_Tool_Scanner_LightOff` | 카탈로그 등록 |
| `SFX.UI.Click` | `/Game/SFX/V2/S_V2_UI_Click` | 카탈로그 등록 |
| `SFX.UI.Hover` | `/Game/SFX/V2/S_V2_UI_Hover` | 카탈로그 등록 |
| `SFX.UI.ContractClear` | `/Game/SFX/V2/S_V2_UI_ContractClear` | 연결 대기 |
| `SFX.UI.GameOver` | `/Game/SFX/V2/S_V2_UI_GameOver` | 연결 대기 |
| `SFX.World.Door.FaultAlarm` | `/Game/SFX/V2/S_V2_World_Door_FaultAlarm` | 카탈로그 등록 |
| `SFX.World.Door.Open` | `/Game/SFX/V2/S_V2_World_Door_Open` | 카탈로그 등록 |
| `SFX.World.Door.Close` | `/Game/SFX/V2/S_V2_World_Door_Close` | 카탈로그 등록 |
| `SFX.World.Door.Jammed` | `/Game/SFX/V2/S_V2_World_Door_Jammed` | 카탈로그 등록 |
| `SFX.Facility.Repair.Start` | `/Game/SFX/V2/S_V2_Facility_Repair_Start` | 카탈로그 등록 |
| `SFX.Facility.Repair.Loop` | `/Game/SFX/V2/S_V2_Facility_Repair_Loop` | 카탈로그 등록 |
| `SFX.Facility.Repair.Cancel` | `/Game/SFX/V2/S_V2_Facility_Repair_Cancel` | 카탈로그 등록 |
| `SFX.Facility.Repair.Complete` | `/Game/SFX/V2/S_V2_Facility_Repair_Complete` | 카탈로그 등록 |
| `SFX.Facility.Repair.Tape` | `/Game/SFX/V2/S_V2_Facility_Repair_Tape` | 카탈로그 등록 |
| `SFX.Facility.Pump.Fault` | `/Game/SFX/V2/S_V2_Facility_Pump_Fault` | 카탈로그 등록 |
| `SFX.Facility.Sorter.Fault` | `/Game/SFX/V2/S_V2_Facility_Sorter_Fault` | 카탈로그 등록 |
| `SFX.Facility.Sorter.FaultLoop` | `/Game/SFX/V2/S_V2_Facility_Sorter_FaultLoop` | 카탈로그 등록 |
| `SFX.Facility.Fingerprint.Request` | `/Game/SFX/V2/S_V2_Facility_Fingerprint_Request` | 카탈로그 등록 |
| `SFX.Facility.Fingerprint.Scan` | `/Game/SFX/V2/S_V2_Facility_Fingerprint_Scan` | 카탈로그 등록 |
| `SFX.Facility.Fingerprint.WaitLoop` | `/Game/SFX/V2/S_V2_Facility_Fingerprint_WaitLoop` | 카탈로그 등록 |
| `SFX.Facility.Fingerprint.Complete` | `/Game/SFX/V2/S_V2_Facility_Fingerprint_Complete` | 카탈로그 등록 |
| `SFX.Facility.Fingerprint.Timeout` | `/Game/SFX/V2/S_V2_Facility_Fingerprint_Timeout` | 카탈로그 등록 |
| `SFX.Facility.Instruction.Use` | `/Game/SFX/V2/S_V2_Facility_Instruction_Use` | 카탈로그 등록 |
| `SFX.Facility.Instruction.Restart` | `/Game/SFX/V2/S_V2_Facility_Instruction_Restart` | 카탈로그 등록 |
| `SFX.Facility.Breaker.Snap` | `/Game/SFX/V2/S_V2_Facility_Breaker_Snap` | 카탈로그 등록 |
| `SFX.Facility.Breaker.Open` | `/Game/SFX/V2/S_V2_Facility_Breaker_Open` | 카탈로그 등록 |
| `SFX.Facility.Breaker.Complete` | `/Game/SFX/V2/S_V2_Facility_Breaker_Complete` | 카탈로그 등록 |
| `SFX.Mothership.Sort.Correct` | `/Game/SFX/V2/S_V2_Mothership_Sort_Correct` | 카탈로그 등록 |
| `SFX.Mothership.Sort.Wrong` | `/Game/SFX/V2/S_V2_Mothership_Sort_Wrong` | 카탈로그 등록 |
| `SFX.Merchant.Purchase.Success` | `/Game/SFX/V2/S_V2_Merchant_Purchase_Success` | 카탈로그 등록 |
| `SFX.Merchant.Purchase.Fail` | `/Game/SFX/V2/S_V2_Merchant_Purchase_Fail` | 카탈로그 등록 |
| `SFX.Merchant.Sell.Success` | `/Game/SFX/V2/S_V2_Merchant_Sell_Success` | 카탈로그 등록 |
| `SFX.Merchant.Loan.Borrow` | `/Game/SFX/V2/S_V2_Merchant_Loan_Borrow` | 카탈로그 등록 |
| `SFX.Merchant.Loan.Repay` | `/Game/SFX/V2/S_V2_Merchant_Loan_Repay` | 카탈로그 등록 |
| `SFX.Merchant.Loan.Interest` | `/Game/SFX/V2/S_V2_Merchant_Loan_Interest` | 카탈로그 등록 |
| `SFX.Mothership.Report.Open` | `/Game/SFX/V2/S_V2_Mothership_Report_Open` | 카탈로그 등록 |
| `SFX.Mothership.Report.Stamp` | `/Game/SFX/V2/S_V2_Mothership_Report_Stamp` | 카탈로그 등록 |
| `SFX.Tool.Crowbar.Pry` | `/Game/SFX/V2/S_V2_Tool_Crowbar_Pry` | 연결 대기 |
| `SFX.Tool.Crowbar.Release` | `/Game/SFX/V2/S_V2_Tool_Crowbar_Release` | 연결 대기 |
| `SFX.Tool.Glass.DrawLoop` | `/Game/SFX/V2/S_V2_Tool_Glass_DrawLoop` | 연결 대기 |
| `SFX.Tool.Glass.Clear` | `/Game/SFX/V2/S_V2_Tool_Glass_Clear` | 연결 대기 |
| `SFX.Clean.Hera.Swing` | `/Game/SFX/V2/S_V2_Clean_Hera_Swing` | 카탈로그 등록 |
| `SFX.Character.Damage.Tick` | `/Game/SFX/V2/S_V2_Character_Damage_Tick` | 카탈로그 등록 |
| `일반 발소리` | `/Game/SFX/N_Footstep/Normal/S_N_Footstep_Normal_01` | 카탈로그 등록 |
| `일반 발소리` | `/Game/SFX/N_Footstep/Normal/S_N_Footstep_Normal_02` | 카탈로그 등록 |
| `일반 발소리` | `/Game/SFX/N_Footstep/Normal/S_N_Footstep_Normal_03` | 카탈로그 등록 |
| `일반 발소리` | `/Game/SFX/N_Footstep/Normal/S_N_Footstep_Normal_04` | 카탈로그 등록 |
| `일반 발소리` | `/Game/SFX/N_Footstep/Normal/S_N_Footstep_Normal_05` | 카탈로그 등록 |


## 검증 결과

- 에디터 빌드 성공. 최종 추가 연결분까지 원본 프로젝트 DLL에 적용하고 에디터를 다시 열었다.
- 저장 후 별도 프로세스에서 SoundWave 79개·카탈로그 78개 항목 재조회 성공. 승인된 피격 피치와 기존 음악·환경음 항목 보존을 확인했다.
- 자동 검사 9개 통과: 오디오 카탈로그·솔로/2인 시설 반복음 3개, 수리지침 4개, 스캐너 라이트 2개. 펌프 누수음의 고장·복구·액터 종료 시 정지도 양쪽 플레이 월드에서 확인했다.
- Ocean 추가 검사 31개 통과: 플레이 중에만 생성한 문·배선반의 수리 시작·취소·테이프·완료, 실제 펌프 누수, 분리수거 결과 표시 호출, 실제 장비 투척, 대출·상환과 이자 알림 호출. 분리수거 결과·이자 검사는 효과음 표시 경로 검증이며 전체 경제 시뮬레이션 검증은 아니다.
- 실제 핫바 전환·헤라 몽타주 효과음 및 폼건 기본 태그 검사 5개 통과. 얼룩 최종 제거음 연결은 코드 검토와 빌드로 확인했다.
- 게임 출력만 녹음한 검증 파일은 무음이 아니며 클리핑이 없었다. 짧은 원본 비교 파일: `Saved/Diagnostics/V2Audio_20261013/V2_SFX_Short_Preview.wav`.
- 검증 종료 후 PIE를 종료하고 임시 설정을 복구했다. 마지막 에디터 상태는 Ocean / 미저장 맵·에셋 0 / Multi-User 연결 해제 상태다.

스캐너 라이트와 사운드 코드·필수 에셋·이 문서는 dev-v2 대상 PR #215에서 공유한다. 오션맵, 별도의 기존 BP 변경, 개인 발표자료와 진단 JSON은 사운드 커밋에서 제외한다.

# 헤라 왼손 그립 수정

- 대상: `/Game/Assets/Character/Shark_MK4/Anim/ABP/ABP_MD_Shark`
- 원인: 헤라 장착 중 `bScraperEquipped=True`지만 `LeftHandIKAlpha=0`. EventGraph의 IK 활성 OR 입력 B가 미연결 false였다.
- 수정: `Get bScraperEquipped` (`6BABEC024032BDF691C440A148741488`) → 기존 OR (`74E205DA459451E725321A8C6F526BDA`) B 연결. 기존 커터+몽타주 조건은 그대로 유지.
- 기존 hand_r 기준 공간의 Grip_L 목표 및 최종 왼손 Two Bone IK를 재사용. 오른손/도구 소켓, AnimGraph 47노드/48연결, 복부 간격 보정, 메시, 손가락 포즈, 시퀀스 모두 변경 없음.
- 백업: `/Game/Revvy/Validation/SharkArmClearance/ABP_MD_Shark_PreScraperGrip`.
- 컴파일 오류/경고 0, 저장 후 BS_UpToDate.

## PIE 확인

- 같은 조건의 백업/수정본 교체 비교: 오른손 위치 차0cm, quaternion 각 성분 차0. 왼손 손목–Grip_L 거리30.59cm → 약6.36e-14cm.
- 헤라 장착 Alpha=1, 커터 대기에서는0으로 정상 복귀.
- 1인칭 조준 -20°/+25°에서도 손목–그립 거리 약5.68e-14cm. 3인칭과 1인칭 화면 확인.
- 이 작업은 왼손 그립 추종을 복구한 것으로, 손가락을 새로 오므리는 애니메이션은 제작하지 않았다. 전체 헤라 작업 어빌리티/멀티플레이/모든 애니메이션 프레임은 미검증.
- 증거: `scraper_grip_front.png`, `scraper_grip_fp.png`, `scraper_grip_final_qa.json`, `scraper_grip_fix_result.json`.
- 검증 중 런타임 AnimClass 교체에서 기존 DeltaTime 나눗셈 경고가 1회 발생했으나 수정본 컴파일 오류/경고는0. 게임플레이 C++/다른 그래프는 수정하지 않음.

"""멀티유저 세션 인지 가드레일.

에디터 자동화(Revvy / UmgMcp / 직접 Python)가 위험한 작업을 하기 전에 호출한다.
멀티유저 세션에 붙어 있는 동안 어떤 작업들은 나 혼자가 아니라 세션 전원에게
영향을 주거나, 남의 작업을 덮어쓰거나, 디스크에 예상 못 한 저장을 일으킨다.

사용법::

    import sys; sys.path.append(r"E:\\Unreal\\ProjectATM\\Tools\\MultiUser")
    import mu_guard

    mu_guard.assert_safe("compile_blueprint")   # 세션 중이면 MultiUserSessionGuard
    mu_guard.report()                           # 현재 상태를 사람이 읽게 출력

왜 엔진의 read-only 역할을 안 쓰는가:
    UConcertClientConfig.ReadOnlyAssignment 가 있지만 VPRoleEvaluator() 델리게이트
    (VirtualProduction 플러그인이 바인딩) 에 의존한다. 검증할 수 없는 의존성을
    끌어오는 대신 이 층에서 막는다.
"""

import unreal


class MultiUserSessionGuard(Exception):
    """멀티유저 세션 중에 허용되지 않는 작업을 시도했을 때."""


# 세션 중 금지. 이유를 같이 적어둔다 — 예외 메시지에 그대로 나간다.
BLOCKED = {
    "compile_blueprint":
        "블루프린트 컴파일은 레벨을 디스크에 저장한다. 세션 샌드박스와 엉켜 "
        "다른 사람의 미저장 변경을 잃을 수 있다.",
    "import_asset":
        "에셋 임포트는 큰 파일을 VPN으로 한꺼번에 밀어내 세션을 끊는다. "
        "(MaxSendRate 를 낮춰놨어도 여전히 위험하다)",
    "save_all":
        "전체 저장은 세션 전원에게 핫리로드를 유발한다. 저장은 각자 필요한 것만.",
    "delete_asset":
        "세션 중 에셋 삭제는 다른 사람의 열려 있는 참조를 깨뜨린다.",
    "create_landscape":
        "랜드스케이프 생성은 대량 데이터를 세션에 밀어넣는다.",
    "modify_material":
        "머티리얼은 실시간 동기화되지 않는다. 저장 시점에 통째로 전파되어 "
        "동시 편집 중이면 한쪽 작업이 사라진다.",
}

# 세션 중에도 허용되지만 한 번 알려준다. MU 가 정상 동기화하는 영역.
WARN = {
    "spawn_actor",
    "move_actor",
    "set_actor_property",
}

# 에이전트 임시 산출물 전용 경로. DefaultConcertSyncCore.ini 의
# ExcludePackageClassFilters 가 이 경로를 세션 전파에서 제외한다.
SCRATCH_PATH = "/Game/_AgentScratch"


def session_state():
    """현재 멀티유저 상태를 dict 로 반환한다. 플러그인이 꺼져 있어도 죽지 않는다."""
    state = {
        "available": False,
        "connected": False,
        "session_name": "",
        "server_name": "",
        "display_name": "",
    }

    statics = getattr(unreal, "MultiUserClientStatics", None)
    if statics is None:
        return state

    state["available"] = True

    try:
        subsystem = unreal.get_engine_subsystem(unreal.MultiUserSubsystem)
        state["connected"] = bool(subsystem and subsystem.is_connected_to_session())
    except Exception:
        # 서브시스템이 없으면 연결 상태만으로 판단한다.
        try:
            status = statics.get_multi_user_connection_status_detail()
            state["connected"] = (str(status).endswith("CONNECTED: 1>"))
        except Exception:
            return state

    if state["connected"]:
        try:
            info = statics.get_multi_user_session_info()
            state["session_name"] = info.get_editor_property("session_name")
            state["server_name"] = info.get_editor_property("server_name")
        except Exception:
            pass

    try:
        me = statics.get_local_multi_user_client_info()
        state["display_name"] = me.get_editor_property("display_name")
    except Exception:
        pass

    return state


def is_in_session():
    return session_state()["connected"]


def assert_safe(operation):
    """`operation` 이 지금 해도 되는 작업인지 확인한다.

    세션 중 금지 작업이면 MultiUserSessionGuard 를 던진다.
    세션 밖이거나 플러그인이 꺼져 있으면 아무 일도 하지 않는다.
    """
    state = session_state()

    if not state["connected"]:
        return state

    if operation in BLOCKED:
        raise MultiUserSessionGuard(
            "멀티유저 세션 '{session}' 에 접속 중이라 '{op}' 을(를) 막았습니다.\n"
            "  이유: {why}\n"
            "  하려면: 세션에서 나간 뒤 작업하고, 끝나면 다시 참가하세요.\n"
            "  임시 산출물이라면 {scratch} 아래에 만들면 세션에 전파되지 않습니다.".format(
                session=state["session_name"] or "(이름 미상)",
                op=operation,
                why=BLOCKED[operation],
                scratch=SCRATCH_PATH,
            )
        )

    if operation in WARN:
        unreal.log_warning(
            "[mu_guard] 세션 '{0}' 중 '{1}' 실행 — 이 변경은 참가자 전원에게 즉시 보입니다.".format(
                state["session_name"] or "(이름 미상)", operation
            )
        )

    return state


def report():
    """사람이 읽을 현재 상태를 출력한다."""
    state = session_state()

    if not state["available"]:
        print("멀티유저 플러그인이 로드되지 않았습니다. 자동화에 제약 없음.")
        return state

    if not state["connected"]:
        print("멀티유저 세션에 접속되어 있지 않습니다. 자동화에 제약 없음.")
        return state

    print("멀티유저 세션 접속 중 — 자동화 제약이 적용됩니다.")
    print("  세션   : {0}".format(state["session_name"]))
    print("  서버   : {0}".format(state["server_name"]))
    print("  내 이름: {0}".format(state["display_name"]))
    print("  금지   : {0}".format(", ".join(sorted(BLOCKED))))
    print("  스크래치 경로: {0}".format(SCRATCH_PATH))
    return state

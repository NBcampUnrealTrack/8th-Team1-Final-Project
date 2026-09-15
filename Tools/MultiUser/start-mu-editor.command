#!/usr/bin/env bash
# Multi-User Editing 세션용 에디터 실행 (macOS 참가자)
#
# Finder 에서 더블클릭하면 실행된다. 안 되면 한 번만:
#     chmod +x Tools/MultiUser/start-mu-editor.command
#
# macOS 에는 Radmin VPN 이 없으므로 이 맥은 Tailscale 경로로 붙는다.
# 호스트(Windows PC)는 0.0.0.0 에 묶여 있어 Radmin 참가자와 같은 세션을 쓴다.
#
# 엔진 경로가 특이하면:  UE_ENGINE_DIR=/path/to/UnrealEngine ./start-mu-editor.command

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_common.sh"

require_python

[ -f "$PROJECT_FILE" ] || die \
    "[오류] MarineDemolition.uproject 를 찾지 못했습니다." \
    "       $PROJECT_FILE" \
    "       이 스크립트는 프로젝트의 Tools/MultiUser/ 안에 있어야 합니다."

ENGINE_DIR="$(find_engine)"
EDITOR="$ENGINE_DIR/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"

SERVER_NAME="$(json_get 'cfg["serverName"]')"

echo "사용 가능한 경로를 확인합니다..."
select_transport

DISPLAY_NAME="${USER}"

# StaticEndpoints 는 Config/DefaultEngine.ini 에 있다 (커밋됨).
# Saved/Config/MacEditor/Engine.ini 에 써도 에디터가 시작하면서 지운다.

cat <<EOF

  경로       : $SELECTED_NAME
  내 주소    : $SELECTED_LOCAL
  호스트     : $SELECTED_HOST:$SELECTED_PORT
  표시 이름  : $DISPLAY_NAME
  엔진       : $ENGINE_DIR
  정적 엔드포인트: Config/DefaultEngine.ini

  에디터를 시작합니다. 툴바의 Multi-User 버튼으로 세션에 참가하세요.

EOF

# 자동 연결(-CONCERTAUTOCONNECT)은 일부러 켜지 않는다.
# 연결 시도가 겹치면 앞선 시도가 취소되면서 'Connection Process Canceled' 로
# 세션이 만들어졌다가 롤백되는 것을 확인했다. 참가는 사용자가 툴바에서 한 번만 누른다.
exec "$EDITOR" "$PROJECT_FILE" \
    "-CONCERTSERVER=$SERVER_NAME" \
    "-UDPMESSAGING_TRANSPORT_UNICAST=$SELECTED_LOCAL:0"

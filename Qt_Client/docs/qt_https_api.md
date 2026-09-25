# Qt API 클라이언트

Qt Client는 config/client_config.ini에 정의된 Raspberry Pi HTTP API에서 앱 시작 시
전체 주차 상태를 조회합니다. API 연결에 실패하면 기존 Mock UI를 유지합니다.

## 설정 우선순위

1. config/client_config.local.ini — 개인 PC 오버라이드, Git 제외
2. config/client_config.ini — 팀 공유 기본값, Git 추적

공유 파일의 키:

~~~ini
[api]
enabled=true
base_url=http://172.20.35.167:8080
slots_path=/api/v1/parking-slots
slot_detail_path=/api/v1/parking-slots/{slot_id}
timeout_ms=5000
reconnect_interval_ms=5000
max_reconnect_interval_ms=60000
allow_insecure_http=true
~~~

allow_insecure_http=false이면 HTTPS URL만 허용합니다.

## 동작

- 앱 시작 시 전체 주차면 조회
- parking_status, active_session, 차량번호, 차량 유형, 점유 시간 반영
- EV_ZONE_VIOLATION, OVERSTAY 경고 반영
- 점유 주차면 클릭 시 상세 정보 요청
- 차량·번호판 원본/개선 이미지 최대 네 장 표시
- JSON, 네트워크, HTTP, 이미지 오류 기록
- 타임아웃과 메모리 이미지 캐시

서버 endpoint와 응답 규격은 [api_v1.md](api_v1.md)를 기준으로 합니다.

## 재연결

- Settings 화면에서 서버 주소 저장 시 `client_config.local.ini`에 기록 후 즉시 재연결
- 전체 주차면 요청 실패 시 `reconnect_interval_ms`부터 재시도
- 연속 실패 시 지수 백오프, `max_reconnect_interval_ms`에서 상한
- 성공 시 재시도 간격 초기화, `Reconnect now`로 수동 즉시 연결

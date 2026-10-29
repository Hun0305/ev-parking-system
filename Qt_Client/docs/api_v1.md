# Smart Parking HTTP API v1

Qt Client가 Raspberry Pi 서버에서 주차 상태와 증거 이미지를 조회할 때 사용하는 계약입니다. 서버 구현과 배포 코드는 이 저장소에 포함하지 않습니다.

## 클라이언트 설정

팀 공용 기본값은 `config/client_config.ini`, 개인 PC 오버라이드는 Git에서 제외되는 `config/client_config.local.ini`에 둡니다.

```ini
[api]
enabled=true
base_url=http://<raspberry-pi-ip>:8080
slots_path=/api/v1/parking-slots
slot_detail_path=/api/v1/parking-slots/{slot_id}
parking_roi_list_path=/api/v1/settings/parking-slots/roi
parking_roi_path=/api/v1/settings/parking-slots/{slot_id}/roi
timeout_ms=5000
reconnect_interval_ms=5000
max_reconnect_interval_ms=60000
allow_insecure_http=true
```

Settings 화면에서 서버 주소를 저장하면 로컬 오버라이드 파일에 기록하고 즉시 재연결합니다. 연결 실패 시 5초부터 최대 60초까지 지수 백오프로 재시도합니다.

## Endpoint

| Method | Path | Client usage |
|---|---|---|
| GET | `/api/v1/health` | 서버 상태 확인용 |
| GET | `/api/v1/parking-slots` | 전체 주차면과 활성 세션 요약 |
| GET | `/api/v1/parking-slots/{slotId}` | 선택한 주차면의 세션과 이미지 목록 |
| GET | `/api/v1/images/{imageId}` | 차량·번호판 증거 이미지 |
| GET | `/api/v1/settings/overstay-threshold` | 장기 점유 판정 기준 조회 |
| PUT | `/api/v1/settings/overstay-threshold` | 장기 점유 판정 기준 변경 |
| GET | `/api/v1/settings/parking-slots/roi` | EV01~EV04 ROI 전체 조회 |
| GET | `/api/v1/settings/parking-slots/{slotId}/roi` | 선택 슬롯 ROI 조회 |
| PUT | `/api/v1/settings/parking-slots/{slotId}/roi` | 정규화 ROI 저장 및 즉시 적용 |

### Overstay threshold

Settings 화면은 시간·분·초 입력을 총 초로 변환하여 PUT하고,
성공 응답 후 GET으로 서버에 실제 저장된 값을 다시 확인합니다.

```json
{"thresholdSeconds": 1800}
```

`thresholdSeconds`의 허용 범위는 60~86400이며, `applyPolicy`는 서버가
반환한 값을 UI에 표시합니다.

### Parking ROI

`Parking ROI` 화면은 기존 CH1 RTSP 디코더의 현재 프레임과 네이티브 Qt
`QGraphicsView` 오버레이를 재사용합니다. 웹페이지나 8091 개발용 프레임 API를
사용하지 않고 다음 정규화 좌표만 PUT합니다.

```json
{"x":0.371528,"y":0.298026,"width":0.113426,"height":0.275658}
```

PUT 성공 후 같은 슬롯을 GET으로 다시 조회해 SQLite에 저장된 서버 좌표를 화면에
반영합니다. 네트워크·HTTP·JSON 오류가 발생하면 마지막으로 성공한 서버 ROI를
유지합니다.

## 응답 필드

전체 목록 응답은 `items` 배열을 사용합니다. 각 항목은 다음 정보를 포함할 수 있습니다.

- `slot_id`
- `parking_status`
- `active_session`
- `plate_number`
- `is_ev`
- `occupied_since` 또는 `elapsed_seconds`
- `alarm`
- `images`

이미지는 `role`(`VEHICLE`, `PLATE`), `processing`(`ORIGINAL`, `ENHANCED`), `url`로 구분합니다. 상대 URL은 `base_url`을 기준으로 해석합니다.

## 오류 처리

- 네트워크·타임아웃·HTTP·JSON 오류는 이벤트 로그에 기록합니다.
- 전체 목록 연결 실패 시 Mock 화면을 유지하고 자동 재연결합니다.
- 상세 조회 실패는 사용자에게 오류를 표시하며 전체 목록 재시도와 중복되지 않습니다.
- HTTP는 `allow_insecure_http=true`일 때만 허용합니다.

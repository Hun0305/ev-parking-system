# Evidence 시간순 조회 개편안

## 목적

현재 Evidence 화면은 주차 칸을 선택한 뒤 해당 칸의 현재 주차 세션과 이미지를 확인하는
구조입니다. 운영자가 여러 칸의 증거 자료를 시간순으로 검토하기에는 조회 기준과 화면
구조가 주차 칸에 종속되어 있습니다.

Evidence의 기본 조회 축을 `slot`에서 `captured_at` 기준의 시간순 기록으로 변경하고,
주차 칸은 필터와 상세 정보로 사용합니다. Parking Map과 Dashboard에서 진입하는 기존
사용 흐름은 사전 필터가 적용된 상태로 유지합니다.

## 현재 구현과 API 호출

현재 Qt Client의 조회 흐름은 슬롯 및 활성 세션 기준입니다.

```text
Evidence에서 슬롯 선택
  -> GET /api/v1/parking-slots/{slot_id}
  -> 응답의 session_id 확인
  -> GET /api/v1/parking-sessions/{session_id}/images
  -> captured_at 기준으로 이미지 variant를 capture group으로 묶음
```

초기 주차 상태 동기화에서는 다음 API가 사용됩니다.

```text
GET /api/v1/parking-slots
```

이 응답의 `images`는 현재 상태에 연결된 이미지로 `slotImages[slotId]`에 저장됩니다.
따라서 현재 클라이언트 모델은 `slot_id -> images` 구조이며, 전체 칸의 과거 이미지를
시간 범위로 조회하는 계약은 아직 없습니다.

이벤트에서 Evidence로 진입하는 경우에도 다음 두 가지 흐름입니다.

- 이벤트에 `session_id`가 있으면 `GET /api/v1/parking-sessions/{session_id}/images`
- `session_id`가 없으면 슬롯 상세를 먼저 조회해 세션을 확인한 뒤 session images를 조회

현재 계약의 기준은 [docs/api_v1.md](api_v1.md)와 다음 구현입니다.

- 슬롯 선택 요청: `src/pages/evidencepage.cpp`의 `requestCurrentEvidence()`
- 슬롯 상세 요청: `src/controllers/parkingcontroller.cpp`의 `requestSlotDetail()`
- 세션 이미지 요청: `src/controllers/parkingcontroller.cpp`의
  `applyParkingSlotDetail()`

## 문제점

1. 사용자가 전체 증거 자료를 시간순으로 보려면 슬롯을 하나씩 선택해야 합니다.
2. 여러 슬롯을 순회하면 슬롯별 상세 요청과 세션 이미지 요청이 반복되어 N+1 요청이
   발생할 수 있습니다.
3. 현재 슬롯 상세 API가 현재 또는 특정 세션 중심으로 동작하면 과거 세션의 자료가
   누락될 수 있습니다.
4. `0 image groups`는 해당 시간대에 자료가 없다는 의미인지, 아직 조회하지 않았다는
   의미인지 슬롯 목록에서 구분하기 어렵습니다.
5. 새 이미지가 들어왔을 때 현재 선택 슬롯만 갱신하는 방식은 전체 시간순 목록을
   유지하기 어렵습니다.

## 권장 UI 구조

### 기본 화면

Evidence 진입 시 최근 24시간 또는 오늘의 자료를 `captured_at` 내림차순으로 표시합니다.
기본 조회 범위는 서버 시간 기준을 명시하고, 화면에는 사용자의 로컬 시간으로 표시합니다.

```text
Evidence
  [기간: 최근 24시간] [슬롯: 전체] [차량번호] [이벤트/사유] [조회]

  2026-08-23 19:06:16 | EV-02 | 34B7788 | OVERSTAY_EVIDENCE | 2 images
  2026-08-23 19:04:03 | EV-01 | 12A3456 | PARKING_CAPTURE   | 1 image
  2026-08-23 18:58:41 | P-03  | -       | VEHICLE_DETECTED   | 2 images
```

### 상세 동작

- 시간순 행을 클릭하면 해당 capture group의 원본 및 처리 이미지를 표시합니다.
- `Open full frame`은 선택한 이미지의 원본을 엽니다.
- 슬롯 값은 행의 필터로 사용하며, 슬롯명을 클릭해 해당 슬롯의 자료만 좁혀볼 수
  있습니다.
- Parking Map에서 들어오면 `slot_id`를 미리 적용합니다.
- Dashboard, Events에서 이벤트를 통해 들어오면 `event_id` 또는 `session_id`를
  미리 적용하고 해당 기록을 선택합니다.
- 새 자료가 도착하면 현재 필터 조건에 해당하는 경우 목록 상단에 추가합니다.
  사용자가 특정 행의 상세를 보고 있으면 선택 행은 유지하고 목록만 갱신합니다.

### 데이터 상태 표시

화면 상태를 다음처럼 분리합니다.

| 상태 | 표시 |
|---|---|
| 조회 전 | 조회 범위를 선택해 Evidence를 조회하세요 |
| 조회 중 | Loading evidence |
| 조회 성공, 자료 없음 | No evidence in this time range |
| 조회 성공, 자료 있음 | 시간순 목록 |
| 오류 | API 오류와 재시도 버튼 |
| 실시간 추가 | New evidence 표시 및 목록 갱신 |

`Not loaded`와 `0 image groups`를 같은 값으로 처리하지 않습니다.

## 제안 API 계약

### Endpoint

```http
GET /api/v1/parking-evidence
```

이 endpoint는 현재 서버에 존재하는 계약이 아니라 신규 추가 제안입니다.

### Query parameters

| Parameter | Required | Description |
|---|---:|---|
| `from` | yes | 조회 시작 시각, ISO-8601 |
| `to` | yes | 조회 종료 시각, ISO-8601 |
| `slot_id` | no | 특정 주차 칸 필터 |
| `plate_number` | no | 차량번호 필터 |
| `reason` | no | 증거 사유 필터 |
| `event_id` | no | 특정 이벤트 필터 |
| `session_id` | no | 특정 세션 필터 |
| `page` | no | 1부터 시작하는 페이지 번호 |
| `limit` | no | 페이지 크기, 서버 최대값 적용 |
| `cursor` | no | 실시간 또는 대용량 목록의 다음 페이지 cursor |

예시:

```http
GET /api/v1/parking-evidence?from=2026-08-23T00:00:00%2B09:00&to=2026-08-23T23:59:59%2B09:00&limit=50
```

### Response

```json
{
  "items": [
    {
      "evidence_id": "EVD-00042",
      "event_id": "EVT-001",
      "slot_id": "EV-02",
      "session_id": 42,
      "plate_number": "34B7788",
      "captured_at": "2026-08-23T19:06:16+09:00",
      "reason": "OVERSTAY_EVIDENCE",
      "images": [
        {
          "image_id": 801,
          "role": "VEHICLE",
          "processing": "ORIGINAL",
          "url": "/api/v1/images/801"
        },
        {
          "image_id": 802,
          "role": "PLATE",
          "processing": "ORIGINAL",
          "url": "/api/v1/images/802"
        }
      ]
    }
  ],
  "next_cursor": null,
  "has_more": false
}
```

### API 규칙

- 결과는 서버에서 `captured_at DESC`, 동일 시각이면 `evidence_id DESC`로 정렬합니다.
- `from`과 `to`는 같은 시간대와 ISO-8601 형식을 사용합니다.
- `to`는 exclusive로 정의해 연속 조회 시 중복을 방지합니다.
- `images`가 비어 있는 증거 기록도 반환할지 여부를 서버 계약에서 결정해야 합니다.
  기본안은 기록은 반환하고 이미지가 없으면 `images: []`로 표시하는 것입니다.
- `limit`의 기본값과 최대값을 정합니다. 기본안은 50, 최대 200입니다.
- URL은 기존 이미지 로더가 처리할 수 있도록 기존 상대 URL 및 API base URL 규칙을
  따릅니다.
- 권한 오류, 잘못된 시간 범위, 서버 오류의 JSON 오류 형식을 기존 API와 통일합니다.

## Qt Client 변경안

### Controller

1. `evidence_path`와 timeout/pagination 설정을 추가합니다.
2. `requestEvidence(from, to, filters, cursor)`를 추가합니다.
3. 응답 parser를 추가해 `EvidenceRecord` 목록으로 변환합니다.
4. `evidenceReady`, `evidenceFailed`, `evidenceUpdated` 신호를 제공합니다.
5. 기존 `requestSlotDetail()`과 `requestEventEvidence()`는 하위 호환용 상세 진입으로
   유지합니다.
6. 실시간 MQTT 또는 상태 변경 이벤트가 발생하면 현재 시간 필터에 해당하는 새 기록을
   목록에 삽입합니다. 서버에 실시간 증거 이벤트가 없으면 짧은 주기의 재조회 대신
   마지막 cursor 기준 polling 정책을 별도로 정합니다.

### Model

현재의 `QHash<QString, QList<ParkingImageResource>> slotImages`와 별도로 다음 모델을
추가합니다.

```cpp
struct EvidenceRecord {
    QString evidenceId;
    QString eventId;
    QString slotId;
    qint64 sessionId = -1;
    QString plateNumber;
    QDateTime capturedAt;
    QString reason;
    QList<ParkingImageResource> images;
};
```

이미지의 `ORIGINAL`과 `ENHANCED`는 서로 다른 Evidence record가 아니라 동일한
`captured_at`과 capture identity 아래의 variant로 그룹화합니다.

### EvidencePage

- 왼쪽 고정 슬롯 목록을 기본 내비게이션에서 필터 패널로 변경합니다.
- 상단에 기간 선택과 조회 버튼을 배치합니다.
- 중앙은 시간순 table/timeline으로 구성합니다.
- 하단 또는 우측에 선택 기록의 이미지 비교 영역을 둡니다.
- 현재 작업 중인 기록과 목록 갱신을 분리해 새 데이터가 들어와도 선택 상태를
  잃지 않게 합니다.

## 단계별 적용 순서

### 1단계: 서버 계약 확정

- endpoint 이름과 권한 정책 확정
- 시간대와 `from/to` 경계 정의
- pagination 및 정렬 기준 확정
- `evidence_id` 또는 capture identity 생성 규칙 확정
- 실시간 추가 통지 방식 결정

### 2단계: 서버 parser 및 Qt Controller

- 응답 fixture 작성
- parser 단위 테스트 작성
- 정상, 빈 결과, 잘못된 기간, pagination, HTTP 오류 테스트
- 기존 슬롯 상세 및 이벤트 상세 흐름 회귀 테스트

### 3단계: 시간순 Evidence UI

- 기간 필터와 시간순 테이블 구현
- 행 선택과 이미지 비교 연결
- `Not loaded`, `Loading`, `Empty`, `Error` 상태 구현
- 이미지 로딩 실패와 full-frame 동작 검증

### 4단계: 실시간 갱신

- 새 기록 append/prepend 정책 구현
- 현재 선택 기록 유지
- 동일 `evidence_id` 중복 방지
- 현재 기간 밖 기록은 목록에 삽입하지 않음

## 완료 기준

- 기간을 지정하면 여러 슬롯의 Evidence가 하나의 시간순 목록에 표시됩니다.
- 슬롯 필터를 적용해 특정 칸의 기록만 볼 수 있습니다.
- 시간순 결과가 서버 정렬 기준과 일치합니다.
- 동일 촬영의 이미지 variant가 한 기록으로 묶입니다.
- 새 Evidence가 도착하면 현재 기간과 필터에 맞는 경우 목록이 갱신됩니다.
- 사용자가 보고 있던 상세 기록과 스크롤 위치가 불필요하게 초기화되지 않습니다.
- API 미조회 상태와 실제 0건 상태가 구분됩니다.
- Dashboard, Parking Map, Events에서 Evidence로 진입하는 기존 경로가 유지됩니다.
- 서버 미지원 상태에서도 기존 슬롯 상세 조회를 fallback으로 사용할지 명확히 정합니다.

## 결정이 필요한 사항

1. 과거 Evidence 보관 기간과 서버 조회 가능 범위
2. 서버 시간과 클라이언트 표시 시간대
3. Evidence record의 고유 ID 생성 주체
4. 삭제되거나 보정된 이미지의 처리 방식
5. 실시간 갱신을 MQTT 이벤트로 할지 polling으로 할지
6. 기존 `/parking-sessions/{session_id}/images`를 신규 endpoint 내부에서 재사용할지
7. 권한에 따른 이미지 URL 접근 정책

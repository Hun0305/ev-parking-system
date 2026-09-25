# Qt Client 모듈 구현·동작 안내서

> 작성 기준: 2026-07-22
> 대상 저장소: `Qt_Client`
> 기준 브랜치: `taejun/rtsp-performance-reconnect`
> 기준 커밋: `bed00ae`
> 대상 독자: 이 코드를 처음 보는 팀원, 리뷰어, Raspberry Pi 서버 연동 담당자

## 1. 문서 목적

이 문서는 현재 Qt Client 코드가 어떤 모듈로 나뉘어 있고, 각 파일과 함수가 어떤 책임을 가지며, 데이터가 어떤 순서로 이동하는지를 설명한다.

특히 다음 내용을 혼동하지 않도록 구분한다.

- 실제 애플리케이션에서 사용하는 운영 코드
- Pi 서버가 없어도 화면을 시연하기 위한 Mock 데이터
- Debug 화면에서 수동으로 넣는 테스트 메시지
- CTest가 자동으로 검증하는 테스트 코드
- 구현은 존재하지만 아직 실제 서버 입력과 연결되지 않은 프로토타입 경로

이 문서는 “현재 코드가 실제로 하는 일”을 기준으로 작성했다. 최종 서버 구조나 앞으로의 계획을 현재 완료 기능처럼 표현하지 않는다.

---

## 2. 먼저 알아야 할 핵심 결론

### 2.1 `ParkingController`는 실제 Controller다

`ParkingController`는 테스트용 이름만 붙인 클래스가 아니다. 실제 애플리케이션에서 `MainWindow`가 생성하고 다음 기능을 맡긴다.

- 주차 상태의 메모리 보관
- Raspberry Pi HTTP/HTTPS API 설정 읽기
- 전체 주차 슬롯 JSON 요청
- API 실패 시 재연결 예약
- 서버 JSON을 화면 상태로 변환
- 이벤트 발생 및 UI signal 전달
- 정규화 문자열 메시지 파싱과 상태 반영

Mock 데이터 값과 시뮬레이션 동작은 Controller 밖의 `src/simulation/`으로 분리되어 있다. Controller는 전달받은 상태를 적용할 뿐 EV-01의 번호판, 초기 점유 목록, 무작위 변경 같은 시연 정책을 알지 못한다.

### 2.2 운영 Controller와 Mock 모듈의 책임이 분리되어 있다

애플리케이션 시작 시 `MainWindow`가 운영 객체와 시뮬레이션 객체를 명시적으로 조립한다.

```text
ParkingController 생성
    ↓
ParkingSimulationService 생성
    ↓
signal/slot 연결
    ↓
ParkingSimulationService::seedInitialState()
    ↓
ParkingController::start()
    ↓
API 연결 성공: 서버 snapshot으로 현재 상태 교체
API 연결 실패: 직전 상태 유지 + 재연결 예약
```

따라서 Pi 서버가 연결되지 않아도 화면에 주차 상태, 번호판, 경고가 표시될 수 있다. 현재 앱은 시연 호환성을 위해 `MainWindow`에서 초기 Mock을 넣고 있으므로, 화면 데이터만 보고 서버 연결 성공으로 판단하면 안 된다. 중요한 차이는 Mock 생성 책임이 `ParkingController`에 섞여 있지 않다는 점이다.

### 2.3 현재 Pi 연동은 최종 실시간 연동이 아니다

현재 구현된 서버 경로는 Qt Network를 이용한 REST 조회 프로토타입이다.

- 구현됨: HTTP/HTTPS `GET`, JSON 파싱, timeout, 재연결, 이미지 조회
- 미구현: 최종 Pi TLS/TCP 상태 스트림
- 미구현: 실제 서버 이벤트 socket receiver
- 미구현: `server slot_id`와 Qt `zoneId`의 확정 매핑
- 미검증: 실제 Pi 서버와의 End-to-End 연동

`processIncomingMessage()`라는 문자열 parser가 있지만, 현재 실제 socket에서 호출되지 않는다. Debug 화면의 수동 입력과 샘플 실행에서 호출된다.

---

## 3. 코드 분류 표기

이 문서에서는 파일과 함수를 다음 기준으로 분류한다.

| 표기 | 의미 |
|---|---|
| **운영** | 실제 앱 실행 경로에서 사용하는 기능 |
| **Mock** | 서버 없이 화면 상태를 만들기 위한 가짜 데이터 또는 동작 |
| **Debug** | 사용자가 Debug 화면에서 수동 실행하는 시뮬레이션 기능 |
| **Test** | CTest 실행 시에만 사용하는 자동 검증 코드 |
| **혼합** | 운영 경로와 Mock/Debug 경로를 함께 연결하는 함수 |
| **프로토타입** | 동작 코드는 있으나 최종 서버 입력 경로와 아직 연결되지 않은 기능 |

---

## 4. 전체 구조

```text
main.cpp
  ├─ RtspVideoItem을 QML 타입으로 등록
  └─ MainWindow 생성
       ├─ CameraSettings
       │    └─ 채널별 RTSP URL 생성
       ├─ DashboardPage
       │    ├─ QQuickWidget × 4
       │    ├─ RtspChannel.qml
       │    └─ RtspVideoItem + FFmpeg worker
       ├─ ParkingSimulationService
       │    ├─ ParkingMockData
       │    └─ Debug/Mock 동작
       ├─ ParkingController
       │    ├─ ApiClient
       │    ├─ ParkingResponseParser
       │    ├─ ParkingViewState
       │    └─ 이벤트 signal
       ├─ ParkingMapPage
       │    ├─ ParkingZoneLayout
       │    ├─ QGraphicsScene
       │    └─ 상태/경고 렌더링 및 레이아웃 편집
       ├─ NotificationCenter
       │    └─ 중요 이벤트 필터링, 미확인 수, ACK/CLEAR 처리
       ├─ EventsPage
       │    └─ 전체 이벤트 표시 및 CSV 내보내기
       ├─ SettingsPage
       │    └─ 카메라 IP와 서버 URL 변경
       └─ DebugPage
            └─ Mock/샘플/수동 문자열 입력
```

### 4.1 주요 데이터 흐름

```text
서버 JSON                          Mock/Debug 입력
    │                                  │
    │                       ParkingSimulationService
    │                                  │
    └──────────────────┬───────────────┘
        ↓
ParkingController
        ↓
ParkingViewState + eventLogged signal
        ├─ MainWindow::renderParkingState()
        │    ├─ ParkingMapPage::render()
        │    └─ DashboardPage::setSummary()
        └─ MainWindow event fan-out
             ├─ Dashboard 최근 이벤트
             ├─ EventsPage 전체 이벤트
             └─ NotificationCenter 중요 알림
```

---

## 5. 디렉터리별 책임

| 경로 | 책임 |
|---|---|
| `src/main.*` | 애플리케이션 진입점과 전체 UI 조립 |
| `src/controllers/` | API, 화면 상태, 이벤트 흐름 조정 |
| `src/models/` | 주차 상태와 Parking Map 레이아웃 모델 |
| `src/api/` | REST 요청, JSON DTO, JSON 파싱, 이미지 다운로드 |
| `src/services/` | 카메라 설정과 알림 정책 |
| `src/simulation/` | Mock 데이터 값과 Debug 시뮬레이션 동작 |
| `src/pages/` | Dashboard, Parking Map, Events, Settings, Debug 화면 |
| `src/dialogs/` | 슬롯 증거 이미지 팝업 |
| `rtsp_legacy/` | FFmpeg RTSP 연결·디코딩·QML 표시 브리지 |
| `qml/` | 채널별 영상 카드 UI |
| `config/` | 공유 예시 설정과 로컬 실행 설정 |
| `tests/` | Controller 재연결, 시뮬레이션 경계, Parking Map 안정성 자동 테스트 |

---

## 6. 애플리케이션 시작 순서

### 6.1 `src/main.cpp` — **운영**

`main()`은 다음 세 단계만 수행한다.

1. `QApplication` 생성
2. C++ 클래스 `RtspVideoItem`을 QML의 `Rtsp 1.0 / RtspVideoItem` 타입으로 등록
3. `MainWindow` 생성 후 Qt event loop 실행

QML은 FFmpeg를 직접 호출하지 않는다. QML에서 보이는 `RtspVideoItem`은 이 등록을 통해 C++ 구현을 사용한다.

### 6.2 `MainWindow::MainWindow()` — **혼합**

파일: `src/mainwindow.cpp`

실행 순서는 다음과 같다.

```text
buildUi()
    ↓
ParkingController 생성
    ↓
ParkingSimulationService 생성
    ↓
connectPages()
    ↓
ParkingSimulationService::seedInitialState()
    ↓
ParkingController::start()
```

`connectPages()`가 먼저 호출되므로 시뮬레이션 서비스가 초기 상태와 이벤트를 넣을 때 Dashboard, Events, NotificationCenter도 함께 갱신된다. 그 뒤 Controller가 실제 API 연결을 시작한다.

---

## 7. 앱 조립 모듈

## 7.1 `src/mainwindow.h/.cpp`

### 파일 책임 — **운영/혼합**

`MainWindow`는 화면을 직접 조립하고 모듈 사이 signal/slot을 연결하는 Composition Root 역할을 한다.

### 주요 함수

| 함수 | 분류 | 동작 |
|---|---|---|
| `MainWindow()` | 혼합 | 운영 Controller와 시뮬레이션 서비스를 생성·연결하고 초기 Demo 상태 주입 후 API 연결 시작 |
| `buildUi()` | 운영 | Sidebar, 페이지, bell/badge, alert banner 생성 |
| `connectPages()` | 운영/Debug 연결 | Controller signal은 화면에, Debug signal은 시뮬레이션 서비스에 연결 |
| `renderParkingState()` | 운영 | `ParkingViewState`를 Map에 전달하고 일반 주차면 집계를 Dashboard에 표시 |
| `showSlotEvidence()` | 프로토타입 | Controller가 준비한 이미지 목록을 `SlotEvidenceDialog`로 표시 |
| `updateNotificationIndicator()` | 운영 | NotificationCenter의 미확인 수로 badge와 bell 스타일 갱신 |
| `showNotificationPopup()` | 운영 | 최대 10개의 활성 알림을 popup에 표시하고 열 때 모두 읽음 처리 |
| `saveCameraIp()` | 운영 | 전체 카메라 IPv4 주소를 검증·저장한 후 4채널 RTSP URL을 다시 계산하여 Dashboard에 반영 |

### 이벤트 fan-out

`ParkingController::eventLogged` 하나가 다음 세 소비자로 전달된다.

```text
eventLogged
  ├─ DashboardPage::prependEvent()     최근 5개
  ├─ EventsPage::appendEvent()         전체 메모리 목록
  └─ NotificationCenter::ingestEvent() 중요 이벤트만 선별
```

이벤트 테이블에 나타났다고 모두 bell 알림이 되는 것은 아니다. NotificationCenter 정책을 통과한 이벤트만 bell에 남는다.

---

## 8. 카메라 설정과 RTSP 영상 모듈

## 8.1 `src/services/camerasettings.h/.cpp`

### 파일 책임 — **운영**

카메라 IP와 채널별 RTSP URL을 설정 또는 환경 변수에서 읽는다.

### `CameraSettings::rtspUrls()`

RTSP URL 우선순위는 다음과 같다.

1. 채널별 환경 변수 `RTSP_CH1_URL`~`RTSP_CH4_URL`
2. CH1/CH2 호환 환경 변수 `RTSP_HIGH_URL`, `RTSP_LOW_URL`
3. 공통 환경 변수 `RTSP_URL` 또는 `HANWHA_RTSP_URL`
4. `camera_config.ini`
5. 실제 설정 파일이 없으면 `camera_config.example.ini`

설정 파일 방식에서는 다음 패턴으로 URL을 만든다.

```text
rtsp://[username:password@]camera_ip:port/channel/profile/media.smp
```

채널별 `profile_ch1`~`profile_ch4`가 있으면 공통 profile보다 우선한다.

### `CameraSettings::saveCameraIp()`

- 사용자가 입력한 전체 문자열이 유효한 IPv4 주소인지 검증한다.
- 기존 주소의 subnet/prefix를 재사용하거나 특정 사설망 대역을 가정하지 않는다.
- 유효한 전체 주소만 `camera_config.ini`의 `camera/camera_ip`에 저장한다.
- 잘못된 입력은 기존 설정을 덮어쓰지 않는다.

실제 credential이 들어갈 수 있는 `config/camera_config.ini`는 Git 제외 대상이다.

## 8.2 `src/pages/dashboardpage.h/.cpp`

### 파일 책임 — **운영**

- 4개 RTSP 채널을 2x2로 배치
- 특정 채널 클릭 시 전체 영역으로 확대
- 주차 집계 표시
- 최근 이벤트 최대 5개 표시

### 주요 함수

| 함수 | 분류 | 동작 |
|---|---|---|
| `createVideoChannel()` | 운영 | `QQuickWidget`을 만들고 채널명, low/high URL 등을 QML context property로 전달 |
| `startDelayedVideoStreams()` | 운영 | 화면 생성 300ms 후 4채널의 `streamEnabled`를 한 번에 활성화 |
| `toggleVideoChannel()` | 운영 | 선택 채널을 2x2 grid 전체 크기로 확대하거나 원복 |
| `setRtspUrls()` | 운영 | 카메라 IP 저장 후 QML source를 갱신 |
| `setSummary()` | 운영 | Total/Occupied/Vacant/Hall error 숫자 표시 |
| `prependEvent()` | 운영 | 최근 이벤트 표 맨 위에 추가하고 5개 초과분 제거 |

현재 코드에서는 4채널이 서로 다른 시간 간격으로 순차 시작되지 않는다. 300ms 타이머가 끝나면 같은 loop에서 모두 활성화된다.

## 8.3 `qml/RtspChannel.qml`

### 파일 책임 — **운영**

한 채널의 영상 카드와 overlay를 표현한다.

- 채널명과 용도 표시
- 연결 상태와 해상도 표시
- `Frame HH:mm:ss.zzz KST` 표시
- 오류 문자열 표시
- 클릭 시 확대/복귀 signal 발생

### 품질 URL 관련 현재 상태

`lowRtspUrl`, `highRtspUrl`, `highQualityEnabled` 속성이 존재한다. 그러나 현재 Dashboard의 확대 동작은 `expanded`만 변경하고 `highQualityEnabled`를 변경하지 않는다.

따라서 현재 실행 경로에서는 high URL을 전달하더라도 확대만으로 자동 전환되지 않는다. 실제 자동 고화질 전환은 후속 기능이다.

## 8.4 `rtsp_legacy/RtspVideoItem.h/.cpp`

### 파일 책임 — **운영**

FFmpeg로 RTSP를 수신·디코딩하고, GUI thread가 그릴 수 있는 `QImage`로 전달한다.

### Thread 구조

```text
Qt GUI thread
  ├─ source 변경
  ├─ 화면 paint
  └─ queued frame 수신
          ↑
std::thread decode worker
  ├─ FFmpeg RTSP TCP 연결
  ├─ packet read
  ├─ video decode
  ├─ BGRA 변환
  └─ 최신 pending frame 교체
```

### 주요 함수

| 함수 | 분류 | 동작 |
|---|---|---|
| `setSource()` | 운영 | 이전 프레임·상태 초기화 후 새 worker 시작 |
| `startWorker()` | 운영 | 이전 worker 종료 후 새 decode thread 생성 |
| `stopWorker()` | 운영 | atomic stop flag 설정 후 thread `join()` |
| `decodeLoop()` | 운영 | FFmpeg 연결, stream/decoder 준비, packet decode, BGRA 변환 |
| `queueDecodedFrame()` | 운영 | worker가 만든 최신 프레임을 pending slot에 저장하고 GUI 전달을 한 번만 예약 |
| `deliverPendingFrame()` | 운영 | GUI thread에서 최신 pending frame만 반영하고 새 pending이 있으면 한 번 더 예약 |
| `handleDecodedFrame()` | 운영 | 화면 프레임, 해상도, 시간, 상태를 갱신하고 repaint 요청 |
| `handleStreamFailure()` | 운영 | 1, 2, 4, 8, 16, 최대 30초 backoff로 재연결 |
| `paint()` | 운영 | aspect ratio를 유지하여 검은 배경 중앙에 프레임 표시 |

### Low-latency 설정

`decodeLoop()`는 다음 FFmpeg option을 사용한다.

- `rtsp_transport=tcp`
- `fflags=nobuffer`
- `flags=low_delay`
- `max_delay=0`
- `reorder_queue_size=0`
- 연결/read timeout 약 3초

Decoder thread 수는 PC 논리 core 수를 기준으로 채널당 `1~4` 범위에서 계산한다.

### 프레임 backlog 방지

모든 디코딩 프레임을 Qt event queue에 쌓지 않는다.

`m_pendingFrame` 하나만 유지하므로 GUI가 늦을 때 오래된 frame은 자연스럽게 최신 frame으로 덮어쓴다. 이 구조는 지연 누적과 메모리 증가를 막기 위한 것이다.

### Frame 시간 계산

1. `best_effort_timestamp`, 없으면 `frame->pts`를 millisecond로 변환한다.
2. FFmpeg가 `start_time_realtime`을 제공하면 stream PTS와 결합한다.
3. 제공하지 않으면 첫 PTS와 현재 PC 시각의 offset을 만든다.
4. 결과를 `Asia/Seoul` 기준 `HH:mm:ss.zzz KST`로 표시한다.

`start_time_realtime`이 없는 카메라에서는 이 값이 카메라가 기록한 절대 시각이라기보다 PC 도착 시각에 맞춘 추정 wall clock이라는 점에 주의한다.

---

## 9. 주차 API와 DTO 모듈

## 9.1 `src/api/parkingmodels.h` — **운영/프로토타입 계약**

서버 JSON을 파싱한 뒤 사용하는 중간 DTO를 정의한다.

### `ParkingImageResource`

- `url`: 이미지 위치
- `timestamp`: 촬영/처리 시각
- `role`: `VEHICLE`, `PLATE`
- `processing`: `ORIGINAL`, `ENHANCED`

### `ParkingSlotSnapshot`

- 서버 slot ID와 상태
- 번호판
- EV 여부와 차량 종류 판정 여부
- 점유 시작 시각 또는 경과 시간
- alarm 문자열
- 이미지 목록

### `ParkingSnapshot`

- snapshot 생성 시각
- 주차 슬롯 목록

## 9.2 `src/api/apiclient.h/.cpp` — **운영/프로토타입**

Qt Network 기반 JSON GET client다.

### `ApiClient::getJson()`

1. base URL과 path 결합
2. HTTPS인지 검사
3. 개발 설정에서 `allow_insecure_http=true`인 경우만 HTTP 허용
4. `Accept: application/json` GET 요청
5. timer로 timeout 처리
6. HTTP status와 JSON parsing 검사
7. 성공 시 `jsonReceived`, 실패 시 `requestFailed` signal 발생

TLS 오류가 발생했을 때 평문으로 자동 fallback하는 코드는 없다.

## 9.3 `src/api/parkingresponseparser.h/.cpp` — **운영/프로토타입**

서버 JSON 키 차이를 일부 흡수하여 DTO로 변환한다.

### 허용하는 배열 키

- `items`
- `data`
- `slots`

### 허용하는 slot key 예

- ID: `slot_id`, `slotId`
- 상태: `parking_status`, `status`, `state`
- session: `active_session`, `activeSession`
- 번호판: `plate_number`, `plateNumber`
- 차량 종류: `vehicle_type`, `vehicleType`, `is_ev`
- 점유 시작: `entry_time`, `entryTime`, `occupied_since`

필수 값인 slot ID 또는 상태가 없으면 전체 parsing을 실패 처리한다.

### 이미지 parsing

두 형식을 지원한다.

1. `images` 배열
2. `images.before`, `images.after` 객체

상대 URL은 이후 Controller에서 API base URL과 결합한다.

## 9.4 `src/api/imageloader.h/.cpp` — **운영/프로토타입**

- HTTPS 또는 명시적으로 허용된 HTTP 이미지만 요청
- 메모리 `QHash<QUrl, QPixmap>` cache 사용
- timeout 및 network error signal 제공
- binary가 유효한 이미지가 아니면 실패 처리

Cache는 프로세스 메모리에만 존재하며 용량 제한과 영속화는 아직 없다.

## 9.5 `src/dialogs/slotevidencedialog.h/.cpp` — **프로토타입**

슬롯의 차량/번호판 원본·개선 이미지를 2x2 grid로 표시한다.

```text
VEHICLE_ORIGINAL | VEHICLE_ENHANCED
PLATE_ORIGINAL   | PLATE_ENHANCED
```

현재 `ParkingMapPage`의 단순 slot 클릭은 서버 상세 조회를 호출하지 않는다. 따라서 dialog와 상세 조회 코드는 존재하지만 Parking Map의 일반 클릭 경로에서는 자동으로 열리지 않는다.

---

## 10. 주차 상태 모델

## 10.1 `src/models/parkingstate.h/.cpp` — **운영**

### 상태를 한 enum으로만 처리하지 않는 이유

현재 화면은 다음 세 축을 분리한다.

```text
점유 상태: Unknown / Vacant / Occupied
차량 종류: Unknown / Electric / General
경고 종류: None / NonEvViolation / Overstay / SensorError
```

`SlotVisualState`가 이 세 축과 ACK 여부를 가진다.

예를 들어 “EV 구역에 일반 차량이 주차하여 경고 발생”은 다음처럼 표현할 수 있다.

```text
occupancy = Occupied
vehicleClass = General
alarm = NonEvViolation
alarmAcknowledged = false
```

이렇게 하면 경고 때문에 점유/차량 색상이 사라지는 문제를 피할 수 있다.

### `ParkingViewState`

Controller가 보관하고 화면에 전달하는 전체 상태다.

- `evSlots`: `EV-xx` 기반 상태
- `parkingSlots`: `P-xx` 기반 상태
- `slotImages`: slot별 증거 이미지
- `slotPlateNumbers`: slot별 번호판
- `apiEnabled`: API 활성화 여부

### 주요 변환 함수

| 함수 | 동작 |
|---|---|
| `normalizeParkingSlotId()` | `EV01 → EV-01`, `P01 → P-01` 정규화 |
| `slotStateFromText()` | 서버/문자열 상태를 `SlotState`로 변환 |
| `slotAlarmKindFromText()` | alarm 문자열을 시각 경고 enum으로 변환 |
| `slotStateText()` | enum을 화면/이벤트 문자열로 변환 |
| `vehicleClassText()` | 차량 종류를 `EV CAR`, `GENERAL CAR` 등으로 변환 |

현재 정규화 함수는 `slot_01`을 Qt의 `EV-01` 또는 `P-01`로 바꾸지 않는다. Pi 서버가 `slot_01` 형식을 보내면 별도 mapping 없이 현재 화면 상태에 적용되지 않는다.

---

## 11. `ParkingController` 상세

파일: `src/controllers/parkingcontroller.h/.cpp`

## 11.1 Controller가 소유하는 것

- `ParkingViewState`
- `ApiClient`
- `ImageLoader`
- API 재연결 timer와 backoff 값
- 설정 경로와 endpoint path

Controller는 Mock 단계, 초기 Mock 값, 무작위 변경 정책을 소유하지 않는다.

## 11.2 함수별 운영/공용 구분

### 시작과 API 연결

| 함수 | 분류 | 상세 동작 |
|---|---|---|
| `ParkingController()` | 운영 | 설정 경로 저장, single-shot reconnect timer 생성 |
| `start()` | 운영 | 실제 API 설정 초기화와 첫 연결 시작 |
| `initializeApiClient()` | 운영 | shared/local 설정 병합, URL·timeout·backoff 읽기 |
| `rebuildApiClient()` | 운영 | 기존 client 연결 해제 후 ApiClient와 ImageLoader 재생성 |
| `reconnectNow()` | 운영 | 전체 슬롯 endpoint에 GET 요청 |
| `scheduleReconnect()` | 운영 | 실패 이유 표시 후 지수 backoff timer 시작 |
| `updateServerBaseUrl()` | 운영 | URL 검증 후 local override에 저장하고 즉시 재연결 |

### 서버 응답 처리

| 함수 | 분류 | 상세 동작 |
|---|---|---|
| `applyParkingSnapshot()` | 운영/프로토타입 | JSON DTO를 `ParkingViewState`에 적용 |
| `applyParkingSlotDetail()` | 운영/프로토타입 | 선택 슬롯 번호판·이미지 목록 갱신 |
| `resetSlotsForSnapshot()` | 운영 | 서버 snapshot 적용 전 EV/P 16개씩 기본 상태로 재생성 |
| `requestSlotDetail()` | 프로토타입 | slot ID의 `-`를 제거하여 detail endpoint 호출 |
| `resolveApiUrl()` | 운영 | 상대 이미지 URL을 API base URL 기준 절대 URL로 변환 |
| `occupiedDurationText()` | 운영 | 서버 경과 시간 또는 시작 시각을 화면용 문자열로 변환 |

`applyParkingSnapshot()`은 현재 `EV-xx` 또는 `P-xx`에 해당하지 않는 ID를 `API_SLOT_SKIPPED`로 기록하고 건너뛴다. Pi 서버의 실제 ID schema와 Qt ID mapping이 먼저 필요한 이유다.

### 상태와 이벤트 처리

| 함수 | 분류 | 상세 동작 |
|---|---|---|
| `replaceViewState()` | 공용 | 외부에서 준비한 전체 `ParkingViewState`를 현재 상태로 교체 |
| `applyEvSlotUpdate()` | 공용 | EV slot의 점유·차량·경고 시각 상태 갱신 |
| `applyParkingSlotUpdate()` | 공용 | 일반 slot의 점유 또는 sensor 경고 갱신 |
| `notifyStateChanged()` | 공용 | `stateChanged` 발생 후 전체 alert banner 갱신 |
| `refreshAlert()` | 공용 | ACK되지 않은 slot 경고를 모아 banner 생성 |
| `recordEvent()` | 공용 | 현재 PC 시각과 함께 `eventLogged` signal 발생 |
| `clearAlarms()` | 운영/Debug 공용 | 현재 활성 경고를 ACK 상태로 변경 |
| `processIncomingMessage()` | 프로토타입/Debug | 정규화 문자열을 해석하여 상태와 이벤트 갱신 |

`replaceViewState()`, `applyEvSlotUpdate()`, `applyParkingSlotUpdate()`는 입력 출처를 구분하지 않는 공용 적용 함수다. 서버 응답과 시뮬레이션 서비스는 이 경계를 통해 상태를 반영하며, Controller는 전달된 값이 Mock인지 판별하지 않는다.

## 11.3 `ParkingMockData`와 `ParkingSimulationService`

파일:

- `src/simulation/parkingmockdata.h/.cpp`
- `src/simulation/parkingsimulationservice.h/.cpp`

`ParkingMockData`는 값만 만든다. QObject, UI, 네트워크, Controller 포인터를 가지지 않는다.

| 함수 | 파일 | 상세 동작 |
|---|---|---|
| `initialViewState()` | `parkingmockdata.cpp` | EV 16개, General 16개 초기 상태 생성 |
| `initialEvents()` | `parkingmockdata.cpp` | 시작 시 표시할 Demo 이벤트 목록 생성 |
| `sampleIncomingMessages()` | `parkingmockdata.cpp` | parser에 넣을 샘플 문자열 7개 반환 |

`ParkingSimulationService`는 동작만 조정한다. 구체적인 상태 적용과 이벤트 기록은 `ParkingController`의 공용 함수에 위임한다.

| 함수 | 분류 | 상세 동작 |
|---|---|---|
| `seedInitialState()` | Mock | `ParkingMockData`의 초기 상태와 이벤트를 Controller에 주입 |
| `toggleMockEv()` | Mock/Debug | `EV-03`을 Vacant/Occupied로 번갈아 변경 |
| `triggerNonEvAlert()` | Mock/Debug | `EV-01`에 비전기차 위반 경고 생성 |
| `triggerOvertimeAlert()` | Mock/Debug | `EV-02`에 장시간 점유 경고 생성 |
| `triggerSensorError()` | Mock/Debug | `P-03`에 Hall sensor 오류 생성 |
| `randomizeParkingSlots()` | Mock/Debug | `P-01~P-16`을 임의 점유/공석으로 변경 |
| `runSampleMessages()` | Debug | 샘플 문자열을 Controller parser에 순서대로 전달 |
| `applyManualMessage()` | Debug | DebugPage의 수동 문자열을 Controller parser에 전달 |

### `initialViewState()`가 만드는 값

```text
EV-01~EV-16
  ├─ 기본: VACANT
  ├─ EV-01: NON_EV_ALERT, plate 12A3456
  └─ EV-02: OVERTIME_ALERT, plate 34B7788

P-01~P-16
  ├─ OCCUPIED: 01, 03, 05, 08, 11, 14
  └─ 나머지: VACANT
```

초기 이벤트도 함께 기록한다.

- EV-01 비전기차 경고
- EV-02 장시간 점유 경고
- 일부 일반 주차면 점유/공석 이벤트

## 11.4 API 성공과 실패 시 차이

### API 성공

```text
현재 상태 존재
    ↓
GET /api/v1/parking-slots 성공
    ↓
JSON parse
    ↓
resetSlotsForSnapshot()
    ↓
알려진 EV-/P- ID 적용
    ↓
Connected signal + UI 갱신
```

### API 실패

```text
현재 상태 존재
    ↓
GET 실패 또는 timeout
    ↓
API_ERROR event
    ↓
직전 상태는 그대로 유지
    ↓
1회 reconnect timer 예약
```

Backoff는 기본 5초에서 시작해 10, 20, 40초처럼 증가하고 설정된 최대값에서 제한된다. 성공하면 초기 간격으로 돌아간다.

현재 성공 후 주기적으로 snapshot을 다시 요청하는 polling timer는 없다. 앱 시작, 주소 변경, 수동 Reconnect, 실패 재시도 시점에 요청한다.

## 11.5 문자열 메시지 parser

`processIncomingMessage()`는 다음 형식을 처리한다.

```text
PARKING_SLOT,P01,OCCUPIED
HALL_SENSOR,P03,ERROR
EV_ALERT,EV01,NON_EV
FIRE_ALARM,CH2,DETECTED
EVENT,CH1,CAMERA_DISCONNECTED,FAILED,RTSP stream disconnected
```

지원 분기:

- `PARKING_SLOT`
- `HALL_SENSOR`, `HALL_SENSOR_EVENT`
- `EV_ALERT`
- `FIRE_ALARM`, `FIRE_EVENT`
- `EVENT`, `DB_EVENT`

현재 이 parser 앞에는 실제 TCP/MQTT/socket adapter가 없다. DebugPage가 호출하는 프로토타입 입력 경로다.

---

## 12. Parking Map 레이아웃 모델

## 12.1 `src/models/parkingzonelayout.h/.cpp`

### `ParkingZoneLayout` 필드

| 필드 | 의미 |
|---|---|
| `zoneId` | Qt 화면 zone ID, 예: `EV-01`, `P-01` |
| `zoneType` | `EV` 또는 `GENERAL` |
| `displayName` | 슬롯 내부 표시 이름 |
| `rect` | scene 좌표와 크기 |
| `rotation` | 회전 각도 |
| `cameraChannel` | `CH1~CH4` |
| `ivaAreaId` | EV zone의 `IVA1~IVA4` |
| `hallSensorId` | Hall sensor 표시/매핑 값 |
| `enabled` | 화면상 사용 여부 |

### 함수 분류

| 함수 | 분류 | 동작 |
|---|---|---|
| `defaultParkingZoneLayout()` | Demo 기본값 | CH1~CH4에 EV 16개와 General 16개 생성 |
| `appendChannelTestLayout()` | Mock/Demo helper | 채널별 4+4 배치를 생성 |
| `loadParkingZoneLayout()` | 운영 | local JSON을 읽고 유효한 zone만 로드 |
| `saveParkingZoneLayout()` | 운영 | 현재 배치를 version 1 JSON으로 저장 |
| `zoneFromJson()` | 운영 | JSON 정규화와 EV/General 규칙 적용 |
| `zoneToJson()` | 운영 | 모델을 JSON field로 변환 |

기본 레이아웃은 실제 주차장 또는 Pi의 authoritative mapping이 아니라 UI 편집을 위한 Demo 기본값이다.

### 레이아웃 파일 우선순위

```text
config/parking_map_layout.local.json 존재 + parsing 성공
    → local layout 사용

그 외
    → defaultParkingZoneLayout() 사용
```

- `parking_map_layout.local.json`: 사용자 PC 전용, Git 제외
- `parking_map_layout.example.json`: 팀 공유 예시

---

## 13. Parking Map 화면

## 13.1 `src/pages/parkingmappage.h/.cpp`

### 파일 책임 — **운영 UI + Demo layout**

- 4채널 주차 배치 표시
- 주차 상태와 경고 표시
- zone 선택과 상세 정보 표시
- zone 추가, 삭제, 이동, resize, 회전
- channel/IVA/Hall mapping 편집
- local JSON 저장과 재로드

### 내부 데이터 구조

```text
m_zones                 저장 가능한 레이아웃 모델
m_lastState             Controller가 전달한 마지막 runtime 상태
m_zoneItems 등 Hash     QGraphicsScene item lookup
m_rebuildingScene       scene clear/rebuild 재진입 방지
m_editMode              편집 UI 활성 상태
m_alarmPulseTimer       미확인 경고 pulse
```

레이아웃 모델과 runtime 상태는 분리되어 있다.

- `m_zones`: 어디에 어떤 zone을 그릴지 결정
- `m_lastState`: 해당 zone을 어떤 상태로 칠할지 결정

두 데이터는 `zoneId`가 같을 때만 연결된다.

## 13.2 `ParkingZoneGraphicsItem` — **운영 UI**

`QGraphicsRectItem`을 확장한 내부 클래스다.

- 내부 drag: 이동
- 좌우 edge: 가로 resize
- 상하 edge: 세로 resize
- corner: 중심 기준 회전
- 우클릭: context menu
- 최소 크기: 16px
- scene 바깥 이동 제한

좌/상 resize 시 회전 좌표를 고려해 item 위치도 같이 보정한다.

## 13.3 주요 함수별 동작

### 렌더링

| 함수 | 동작 |
|---|---|
| `render()` | 마지막 runtime 상태 저장 후 전체 visual, alarm timer, table, editor 갱신 |
| `visualStateForZone()` | EV/General runtime 상태를 공통 `SlotVisualState`로 변환 |
| `updateZoneVisual()` | fill, border, label, tooltip, siren, halo 갱신 |
| `updateAlarmAnimationState()` | 미확인 경고가 있을 때만 90ms pulse timer 실행 |
| `updateAlarmPulse()` | sin 곡선으로 halo와 siren opacity 변경 |

### 편집

| 함수 | 동작 |
|---|---|
| `addGeneralZone()` | 충돌하지 않는 `P-xx` ID와 빈 위치를 찾아 추가 |
| `addEvZone()` | 충돌하지 않는 `EV-xx` ID와 IVA를 찾아 추가 |
| `deleteSelectedZone()` | 선택 zone을 모델에서 제거하고 scene 재구성 |
| `applyEditorFields()` | 우측 editor 값으로 ID/type/channel/geometry 갱신 |
| `handleZoneItemMoved()` | item 좌표를 모델에 반영하고 중심점 기준 channel 자동 판정 |
| `applyZoneTypeRules()` | General의 IVA 제거, EV의 IVA 유효성과 채널 중복 확인 |
| `saveLayout()` | item geometry를 모델에 반영한 후 local JSON 저장 |
| `reloadLayout()` | local 또는 default 레이아웃 다시 로드 |
| `resetDefaultLayout()` | Demo 기본 32 zone으로 되돌리되 자동 저장하지 않음 |

### ID 충돌 방지

`nextZoneId()`는 두 목록을 모두 검사한다.

1. 현재 레이아웃 `m_zones`
2. 마지막 runtime 상태 `m_lastState`

따라서 `EV-01~EV-16`, `P-01~P-16` runtime 상태가 있으면 신규 zone은 `EV-17`, `P-17`부터 시작한다.

새 zone은 runtime 상태가 없으므로 `WAITING DATA`로 표시된다.

### 빈 위치와 staging

`nextZoneRectForChannel()`은 선택 채널의 4x2 후보 위치를 순서대로 검사한다.

- 기존 zone과 padding 포함 교차하지 않으면 사용
- 채널 8자리가 모두 차면 y=590 아래 staging 영역 사용
- staging은 한 줄에 9개 배치
- zone 수에 맞춰 scene 높이를 동적으로 확장
- 신규 item을 `ensureVisible()`로 화면에 표시

### Scene 수명 안전 처리

`rebuildScene()`은 다음 순서를 지킨다.

1. `m_rebuildingScene = true`
2. `QSignalBlocker`로 selection signal 차단
3. scene item을 가리키는 raw pointer hash를 먼저 비움
4. `QGraphicsScene::clear()`
5. scene과 item 재생성
6. `m_rebuildingScene = false`

소멸자에서는 scene → page signal 연결을 해제하여 page 멤버가 먼저 파괴되는 과정의 재진입을 막는다.

### 현재 편집 모드의 실제 의미

우측 editor widget은 Edit mode에서만 활성화된다. 그러나 scene item은 `syncItemsEditable()`에서 항상 movable로 설정되고, View mode에서 drag를 시작하면 자동으로 Edit mode가 켜진다.

즉 현재 View mode는 완전한 read-only lock이 아니다.

### 미저장 상태 표시 한계

추가, 삭제, 이동, 필드 변경, 기본값 복원 후 `Unsaved changes` 문구를 보여준다. 그러나 별도의 dirty flag, 종료 확인 dialog, 자동 복구 기능은 아직 없다.

---

## 14. Notification과 이벤트 모듈

## 14.1 `src/services/notificationcenter.h/.cpp` — **운영/프로토타입**

### 책임

- 모든 이벤트 중 중요한 이벤트만 bell 알림으로 유지
- severity와 title 생성
- 동일 source/type 알림 중복 교체
- 미확인 개수 계산
- ACK/CLEAR 수신 시 기존 알림 제거
- 최근 활성 알림 최대 10개 유지

### 대표 severity

| Event | Severity |
|---|---|
| `FIRE_ALARM`, `FLAME_DETECTED` | `CRITICAL` |
| `HALL_SENSOR_ERROR` | `WARNING` |
| `CAMERA_DISCONNECTED` | `WARNING` |
| `API_ERROR`, `API_PARSE_ERROR` | `WARNING` |
| `NON_EV_ALERT`, `OVERTIME_ALERT` | `WARNING` |

`RECORDED`, `DONE`, `SKIPPED`, `REJECTED` 상태는 일반적으로 bell 알림에서 제외된다.

### ACK/CLEAR 처리

- source가 `ALL`이면 모든 활성 알림 제거
- 같은 source와 같은 alert group 제거
- FIRE와 FLAME은 같은 그룹으로 취급
- `CAMERA_RECONNECTED`는 `CAMERA_DISCONNECTED` 제거
- `ALARM_ACK`는 같은 source의 alarm/alert/error 제거

알림은 DB에 저장되지 않고 프로세스 메모리에만 있다.

## 14.2 `src/pages/eventspage.h/.cpp` — **운영**

- event를 순서대로 table에 추가
- 사용자가 선택한 경로로 CSV 저장
- UTF-8 BOM과 CSV quote escaping 적용
- 저장 성공/실패를 다시 Controller event로 기록

## 14.3 `src/pages/debugpage.h/.cpp` — **Debug 전용**

이 파일은 실제 sensor나 Pi 연결을 구현하지 않는다. 사용자가 테스트 동작을 직접 실행하도록 signal만 발생시킨다.

| UI 버튼 | 연결 대상 |
|---|---|
| Clear alarms | `ParkingController::clearAlarms()` |
| Toggle mock EV | `ParkingSimulationService::toggleMockEv()` |
| Test non-EV | `ParkingSimulationService::triggerNonEvAlert()` |
| Test overtime | `ParkingSimulationService::triggerOvertimeAlert()` |
| Test Hall sensor error | `ParkingSimulationService::triggerSensorError()` |
| Randomize parking | `ParkingSimulationService::randomizeParkingSlots()` |
| Run RX sample | `ParkingSimulationService::runSampleMessages()` |
| Apply message | `ParkingSimulationService::applyManualMessage(text)` |

DebugPage에 표시되는 기본 문자열 `EV_ALERT,EV01,NON_EV`는 실제 수신 데이터가 아니라 입력 예시다.

---

## 15. 설정 화면

## 15.1 `src/pages/settingspage.h/.cpp` — **운영/프로토타입**

### 카메라 설정

- 화면에서 전체 IPv4 주소 입력
- 저장 요청은 `MainWindow::saveCameraIp()`로 전달
- 저장 성공 후 4채널 RTSP URL 재생성

### 서버 설정

- subnet 가정 없이 전체 API base URL 입력
- Save and reconnect: local override 저장 후 client 재생성
- Reconnect now: 현재 설정으로 snapshot 재요청
- Controller의 connection signal을 label 색상과 문구로 표시

서버 주소는 `config/client_config.local.ini`에 저장되어 Git에서 제외된다.

---

## 16. 기능별 End-to-End 동작

## 16.1 RTSP 영상 표시

```text
camera_config.ini / RTSP_* 환경 변수
    ↓
CameraSettings::rtspUrls()
    ↓
DashboardPage::createVideoChannel()
    ↓
QML context property
    ↓
RtspChannel.qml
    ↓
RtspVideoItem::setSource()
    ↓
std::thread + FFmpeg decodeLoop()
    ↓
latest pending QImage
    ↓
Qt GUI thread deliverPendingFrame()
    ↓
paint() + Frame KST overlay
```

## 16.2 주차 API 연결 성공

```text
MainWindow 생성
    ↓
ParkingSimulationService::seedInitialState()
    ↓
ParkingController::replaceViewState()
    ↓
ParkingController::start()
    ↓
ApiClient GET slots_path
    ↓
ParkingResponseParser::parseSnapshot()
    ↓
ParkingController::applyParkingSnapshot()
    ↓
ParkingViewState 교체
    ↓
stateChanged
    ↓
Parking Map + Dashboard 갱신
```

## 16.3 주차 API 연결 실패

```text
GET timeout/network error
    ↓
requestFailed
    ↓
API_ERROR event
    ↓
직전 상태 유지
    ↓
Disconnected / retry in N seconds
    ↓
QTimer 재요청
```

## 16.4 Debug sample 실행

```text
DebugPage Run RX sample
    ↓
sampleMessagesRequested
    ↓
ParkingSimulationService::runSampleMessages()
    ↓
ParkingMockData::sampleIncomingMessages()
    ↓
각 문자열을 ParkingController::processIncomingMessage()
    ↓
상태 갱신 + eventLogged
    ↓
Map/Dashboard/Events/Notification 갱신
```

## 16.5 Parking Map 레이아웃 편집

```text
local JSON 또는 Demo default load
    ↓
rebuildScene()
    ↓
사용자 add/move/resize/rotate/edit/delete
    ↓
m_zones 갱신
    ↓
Unsaved changes 표시
    ↓
Save layout
    ↓
parking_map_layout.local.json
```

## 16.6 알림 처리

```text
Controller recordEvent()
    ↓
MainWindow eventLogged handler
    ↓
NotificationCenter::ingestEvent()
    ├─ 중요하지 않음 → 무시
    ├─ 신규 중요 이벤트 → 추가/교체
    └─ ACK/CLEAR → 기존 matching 알림 제거
    ↓
notificationsChanged
    ↓
bell style + badge + popup
```

---

## 17. Mock/Test 코드 빠른 찾기

## 17.1 Mock 데이터가 들어 있는 파일과 함수

| 파일 | 함수/위치 | 내용 |
|---|---|---|
| `src/simulation/parkingmockdata.cpp` | `initialViewState()` | 초기 EV/P 상태와 번호판·경고 |
| `src/simulation/parkingmockdata.cpp` | `initialEvents()` | 초기 Demo 이벤트 |
| `src/simulation/parkingmockdata.cpp` | `sampleIncomingMessages()` | 샘플 문자열 7개 |
| `src/simulation/parkingsimulationservice.cpp` | `seedInitialState()` | 초기 상태와 이벤트 주입 |
| `src/simulation/parkingsimulationservice.cpp` | `toggleMockEv()` 등 | Debug 시뮬레이션 동작 |
| `src/simulation/parkingsimulationservice.cpp` | `runSampleMessages()` | 샘플 문자열을 parser에 전달 |
| `src/pages/debugpage.cpp` | 생성자 | Mock/Debug 버튼과 기본 입력 예시 |
| `src/models/parkingzonelayout.cpp` | `defaultParkingZoneLayout()` | Demo용 32 zone 배치 |
| `config/parking_map_layout.example.json` | 전체 | 팀 공유 Demo 레이아웃 |

## 17.2 실제 동작 코드가 들어 있는 파일과 함수

| 파일 | 함수/위치 | 내용 |
|---|---|---|
| `src/controllers/parkingcontroller.cpp` | `initializeApiClient()` | API 설정 읽기 |
| `src/controllers/parkingcontroller.cpp` | `reconnectNow()` | 실제 JSON GET 요청 |
| `src/controllers/parkingcontroller.cpp` | `applyParkingSnapshot()` | 서버 snapshot 반영 |
| `src/api/apiclient.cpp` | `getJson()` | Qt Network 요청 |
| `src/api/parkingresponseparser.cpp` | `parseSnapshot()` | JSON 검증·변환 |
| `rtsp_legacy/RtspVideoItem.cpp` | `decodeLoop()` | 실제 RTSP/FFmpeg 수신·디코딩 |
| `src/services/camerasettings.cpp` | `rtspUrls()` | 실제 채널별 URL 구성 |
| `src/pages/parkingmappage.cpp` | `render()` | Controller 상태 렌더링 |
| `src/models/parkingzonelayout.cpp` | `load...`, `save...` | local layout 입출력 |
| `src/services/notificationcenter.cpp` | `ingestEvent()` | 중요 이벤트 분류 |

## 17.3 자동 테스트 데이터가 들어 있는 파일

| 파일 | 데이터 | 검증 목적 |
|---|---|---|
| `tests/parkingcontroller_retry_test.cpp` | 임시 INI와 연결 불가능한 `127.0.0.1:1` | 실제 Controller 재연결 signal |
| `tests/parkingsimulationservice_test.cpp` | 실제 MockData와 SimulationService | Mock 값·동작과 Controller 공용 경계 |
| `tests/parkingmappage_add_slot_test.cpp` | EV/P 16개씩 만든 runtime 상태 | 신규 ID 충돌 방지와 `WAITING DATA` |

---

## 18. 자동 테스트 상세

## 18.1 `parkingcontroller_retry`

### 파일

`tests/parkingcontroller_retry_test.cpp`

### 사용하는 코드

테스트용 가짜 Controller를 만들지 않는다. 실제 `ParkingController`, `ApiClient`, parser, model 소스를 test executable에 함께 compile한다.

### 주입 데이터

임시 INI 파일:

```ini
[api]
enabled=true
base_url=http://127.0.0.1:1
timeout_ms=200
reconnect_interval_ms=1000
max_reconnect_interval_ms=1000
allow_insecure_http=true
```

### 검증 항목

- `Connecting...` 상태가 두 번 이상 발생
- 중간에 `retry in` 상태가 발생
- 4초 이내 만족하지 않으면 실패

### 검증하지 않는 항목

- 실제 Pi 연결
- 정상 서버 JSON 적용
- slot ID mapping
- 이미지 다운로드
- 최종 TLS/TCP 이벤트 수신

## 18.2 `parkingmappage_add_slot`

### 파일

`tests/parkingmappage_add_slot_test.cpp`

### 주입 데이터

- `EV-01~EV-16`
- `P-01~P-16`
- 홀수/짝수 기준 Vacant/Occupied 상태

### 검증 항목

1. General/EV를 번갈아 12개 추가
2. 새 zone이 `WAITING DATA`인지 확인
3. scene 높이가 560을 넘어 staging이 생기는지 확인
4. Delete/Add 20회 반복
5. 최종 zone 개수 유지
6. page 소멸 과정에서 crash가 없는지 간접 확인

테스트는 `QT_QPA_PLATFORM=offscreen`에서 실행된다.

## 18.3 `parking_simulation_service`

### 파일

`tests/parkingsimulationservice_test.cpp`

### 사용하는 코드

테스트 전용 Mock 복사본을 만들지 않고 실제 `ParkingMockData`, `ParkingSimulationService`, `ParkingController`를 함께 compile한다. 이를 통해 테스트 코드는 `tests/`에만 있고, 시연용 데이터와 동작은 `src/simulation/`, 운영 적용 로직은 `src/controllers/`에 남는 경계를 검증한다.

### 검증 항목

1. 초기 EV 16개와 General 16개 생성
2. EV-01 비전기차 경고, EV-02 장시간 점유 경고, 초기 이벤트 5개 확인
3. EV-03 두 번 전환 후 점유 상태와 번호판 확인
4. Hall sensor 오류 동작 확인
5. 수동 메시지와 샘플 메시지가 Controller parser를 통해 상태에 반영되는지 확인

## 18.4 실행 명령

```powershell
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build build
& 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --test-dir build --output-on-failure
```

---

## 19. 설정 파일과 보안

## 19.1 Git에 올리는 파일

- `config/camera_config.example.ini`
- `config/client_config.example.ini`
- `config/parking_map_layout.example.json`

예시 파일에는 실제 credential을 넣지 않는다.

## 19.2 Git에 올리지 않는 파일

- `config/camera_config.ini`
- `config/client_config.local.ini`
- `config/parking_map_layout.local.json`
- `.env`, `*.env`
- `third_party/ffmpeg/`

## 19.3 API 설정 우선순위

`ParkingController::initializeApiClient()`는 같은 key가 local 설정에 있으면 local 값을 사용하고, 없으면 shared 설정을 사용한다.

```text
client_config.local.ini
    ↓ 없으면
client_config.ini
    ↓ 없으면
코드 default
```

## 19.4 HTTP 사용 주의

현재 개발 설정은 `allow_insecure_http=true`를 사용할 수 있다. 최종 배포는 HTTPS/TLS-first가 기준이며, 이 값을 `false`로 두면 `https://`만 허용한다.

---

## 20. 현재 한계와 오해하면 안 되는 부분

1. **화면에 데이터가 보인다고 Pi 연결 성공이 아니다.**
   현재 `MainWindow`가 시연용 초기 상태를 먼저 주입한다.

2. **Mock 코드가 분리됐다고 Release에서 자동 비활성화되는 것은 아니다.**
   현재는 실행 호환성을 위해 `ParkingSimulationService::seedInitialState()`를 항상 호출한다. 실제 배포 단계에서는 설정 또는 build option으로 주입 여부를 제어할 수 있다.

3. **현재 REST snapshot은 실시간 상태 stream이 아니다.**
   성공 후 주기 polling도 아직 없다.

4. **`processIncomingMessage()`는 실제 socket receiver가 아니다.**
   현재는 Debug 입력으로만 호출된다.

5. **Pi의 `slot_01`과 Qt의 `EV-01`/`P-01`은 자동 연결되지 않는다.**
   명시적인 mapping schema가 필요하다.

6. **Parking Map 기본 배치는 Demo 값이다.**
   실제 주차장 구조나 서버 authoritative mapping이 아니다.

7. **Parking Map 클릭은 현재 서버 상세 조회를 자동 실행하지 않는다.**
   편집/선택과 서버 요청을 분리한 상태다.

8. **RTSP high URL은 전달되지만 확대 시 자동 활성화되지 않는다.**
   `highQualityEnabled`를 전환하는 UI/연결이 아직 없다.

9. **알림은 최대 10개, 메모리 전용이다.**
   앱을 종료하면 사라진다.

10. **레이아웃의 `Unsaved changes`는 문구일 뿐 완전한 dirty-state 관리가 아니다.**
    종료 전 저장 확인 dialog는 없다.

11. **증거 이미지 경로는 구현돼도 사용자 진입 경로가 제한돼 있다.**
    Map 클릭만으로 detail API가 호출되지 않는다.

12. **현재 자동 테스트는 범위가 좁다.**
    RTSP, NotificationCenter, API 정상 응답, 실제 Pi E2E 테스트는 없다.

---

## 21. 새 팀원이 기능을 수정할 때 찾을 위치

| 수정 목적 | 먼저 볼 파일/함수 |
|---|---|
| API 연결 주소·재시도 | `ParkingController::initializeApiClient()`, `scheduleReconnect()` |
| 서버 JSON schema | `parkingmodels.h`, `ParkingResponseParser` |
| Pi slot ID와 Qt zone 매핑 | `applyParkingSnapshot()`, `normalizeParkingSlotId()`, `ParkingZoneLayout` |
| Mock 초기 데이터 | `ParkingMockData::initialViewState()`, `initialEvents()` |
| Mock/Debug 동작 | `ParkingSimulationService` |
| Debug sample 문자열 | `ParkingMockData::sampleIncomingMessages()`, `DebugPage` |
| 공용 상태 적용 | `ParkingController::replaceViewState()`, `applyEvSlotUpdate()`, `applyParkingSlotUpdate()` |
| 슬롯 상태 색상·경고 | `ParkingViewState`, `ParkingMapPage::updateZoneVisual()` |
| Parking Map 추가 위치 | `nextZoneRectForChannel()` |
| Parking Map ID 생성 | `nextZoneId()` |
| Parking Map drag/resize/rotation | `ParkingZoneGraphicsItem` |
| Scene crash 예방 | `rebuildScene()`, `~ParkingMapPage()` |
| 알림 대상 정책 | `NotificationCenter::isNotifiableEvent()` |
| ACK/CLEAR grouping | `NotificationCenter::isSameAlertGroup()` |
| RTSP URL 생성 | `CameraSettings::rtspUrls()` |
| RTSP reconnect | `RtspVideoItem::handleStreamFailure()` |
| RTSP decoding | `RtspVideoItem::decodeLoop()` |
| Frame KST 표시 | `frameClockText()`, `RtspChannel.qml` |
| 테스트 추가 | `tests/`, `CMakeLists.txt`의 `BUILD_TESTING` 블록 |

---

## 22. 다음 연동 단계의 권장 경계

Mock 값과 동작은 이미 Controller 밖으로 분리됐다. 실제 Pi 연동 단계에서는 서버 입력 Adapter를 같은 수준의 별도 경계로 추가하는 방향이 자연스럽다.

```text
ParkingMockData ──→ ParkingSimulationService ──┐
                                              ├─→ ParkingController ─→ UI signals
Pi server ───────→ PiServerStateAdapter ──────┘
```

실제 Pi server schema가 확정되기 전에 형식적인 interface/factory를 만들 필요는 없다. 먼저 다음 계약을 확정해야 한다.

1. `server slot_id ↔ Qt zoneId` mapping
2. 점유 상태 schema
3. 경고와 ACK/CLEAR event schema
4. timestamp 기준
5. snapshot과 실시간 event의 관계
6. Demo 초기 상태를 개발 환경에서만 활성화할 설정 또는 build option

---

## 23. 용어 정리

| 용어 | 의미 |
|---|---|
| Controller | 입력을 받아 모델을 갱신하고 UI signal을 발생시키는 조정자 |
| DTO | 서버 JSON을 파싱한 중간 데이터 구조 |
| Snapshot | 특정 시점의 전체 주차 상태 목록 |
| Runtime state | 현재 화면이 표시할 점유·차량·경고 상태 |
| Layout | zone의 위치·크기·회전·채널·IVA·Hall mapping |
| Mock | 외부 서버 없이 시연하기 위해 코드가 만든 데이터 |
| Debug input | 사용자가 화면에서 수동으로 넣는 테스트 문자열 |
| ACK | 경고를 확인했음을 표시하는 상태 |
| CLEAR | 경고 원인이 해소되어 알림을 제거하는 상태 |
| PTS | 영상 frame의 presentation timestamp |
| Backoff | 실패가 반복될수록 재시도 간격을 늘리는 정책 |

---

## 24. 요약

현재 Qt Client는 다음 기능을 실제 코드로 가지고 있다.

- FFmpeg 기반 4채널 RTSP 수신과 재연결
- Frame PTS 기반 KST 시각 표시
- REST JSON 요청·파싱·재연결
- 주차 상태 모델과 경고 시각 분리
- 2D Parking Map 표시·편집·저장
- 중요 이벤트 notification bell
- 이벤트 표와 CSV 내보내기
- Controller 재연결, 시뮬레이션 경계, Parking Map 안정성 테스트

동시에 다음 Mock/Debug 기능도 포함한다.

- 시작 시 32개 주차 상태 생성
- 비전기차·장시간 점유·sensor 오류 시뮬레이션
- 무작위 점유 상태
- 샘플 수신 문자열
- Demo Parking Map 기본 배치

따라서 현재 코드는 “운영 Controller와 시연용 데이터·동작이 파일과 객체 수준에서 분리되어 있고, MainWindow가 두 경계를 조립한다”라고 이해하면 된다. 다음 연동 단계의 핵심은 Controller를 새로 만드는 것이 아니라 Pi server의 확정 schema와 ID mapping을 별도 입력 Adapter로 연결하고, 배포 환경에서 Demo 초기 주입을 끄는 것이다.

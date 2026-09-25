# 2026-07-22 Debug 진단 콘솔 개발 일지

## 1. 작업 목적

기존 Debug 페이지는 Mock 상태 변경과 문자열 RX 주입 버튼만 제공했다. 이 구조로는 화면이 표시되는데 Pi 서버는 연결되지 않은 상황이나, 특정 RTSP 채널만 멈춘 상황을 한 번에 판단하기 어려웠다.

이번 작업에서는 Debug 메뉴의 주 역할을 시뮬레이션에서 통합 진단으로 변경했다.

```text
기존
DebugPage
  └─ Simulation buttons

변경
DebugPage
  ├─ Overview
  ├─ Live Logs
  └─ Test Tools
```

시뮬레이션 기능은 삭제하지 않고 `Test Tools` 탭으로 격리했다. 테스트 동작은 계속 `ParkingSimulationService`가 담당하며 `ParkingController` 내부로 다시 들어가지 않는다.

## 2. 새 진단 상태 모델

추가 파일:

```text
src/diagnostics/diagnostictypes.h
src/diagnostics/diagnosticsservice.h
src/diagnostics/diagnosticsservice.cpp
```

주요 상태:

```text
ApiDiagnosticState
  - enabled
  - connected
  - status
  - endpoint
  - lastError
  - lastAttemptAt
  - lastSuccessAt
  - lastLatencyMs
  - lastHttpStatus
  - consecutiveFailures
  - nextRetrySeconds
  - appliedSlotCount

RtspChannelDiagnostic
  - channel
  - configured
  - status
  - error
  - resolution
  - startupDelayMs
  - lastFrameWallClockMs

ParkingDiagnosticState
  - dataSource
  - slotCount
  - activeAlarmCount
  - lastServerSlotCount
  - lastServerSyncAt
  - lastSimulationScenario
```

`DiagnosticsService`는 상태를 소유하는 Controller가 아니라 여러 모듈의 진단 스냅샷을 모으는 집계기다. 주차 상태와 RTSP 재생 상태를 직접 변경하지 않는다.

## 3. 데이터 출처 표시

Debug Overview 상단에 현재 주차 상태의 출처를 표시한다.

```text
UNKNOWN
  아직 Mock 또는 서버 상태가 적용되지 않음

MOCK
  초기 Mock 상태만 적용됨

SERVER
  마지막 유효 상태가 Pi API snapshot에서 적용됨

MIXED
  서버 snapshot 적용 이후 로컬 Test Tools가 상태를 변경함
```

현재 개발 환경에서는 초기 Mock 32개가 먼저 표시되고 Pi API 연결이 실패하므로 다음과 같이 표시된다.

```text
Runtime data: MOCK
Pi API: RETRYING
Parking state: 32 slots
```

이제 화면에 데이터가 보이는 사실과 서버 연결 성공 여부를 혼동하지 않아도 된다.

## 4. Pi API 진단

`ApiClient` 결과 신호에 요청 지연 시간과 HTTP 상태를 추가했다.

`ParkingController`는 다음 전환마다 `ApiDiagnosticState`를 발행한다.

```text
DISABLED
  ↓
DISCONNECTED
  ↓
CONNECTING
  ├─ 성공 → CONNECTED
  ├─ 파싱 실패 → INVALID_RESPONSE
  └─ 연결 실패 → RETRYING
```

표시 정보:

- credential과 query를 제거한 endpoint
- 마지막 요청 지연 시간
- 마지막 HTTP 상태
- 연속 실패 횟수
- 다음 재시도 시간
- 마지막 정상 동기화 시각
- 마지막 오류
- 마지막 적용 슬롯 수

URL은 로그와 진단 화면에 표시하기 전에 user info, query, fragment를 제거한다.

## 5. RTSP 진단

기존 `RtspVideoItem`에는 이미 다음 정보가 있었다.

```text
status
errorString
videoSize
startupDelayMs
frameWallClockMs
```

이 값을 QML root의 읽기 전용 진단 property로 전달하고, `DashboardPage`가 1초마다 CH1~CH4 스냅샷을 수집한다.

1초 polling을 선택한 이유:

- 프레임마다 QWidget 진단 표를 갱신하지 않음
- 4채널 합계 약 120회/초 signal 발생 방지
- 운영자가 확인하기에는 1초 주기가 충분함

Debug 표 표시 항목:

```text
Channel | Configured | State | Resolution | Startup | Frame age | Last error
```

수동 QA에서 `Playing` 상태와 RTSP 오류 문자열이 동시에 남는 순간이 확인됐다. 상단 요약이 이 상태를 정상으로 계산하지 않도록 다음 유효 상태 판정을 추가했다.

```text
PLAYING
  status=Playing, error 없음, frame age 3초 이하

DEGRADED
  status=Playing이지만 error가 남아 있음

STALE
  status=Playing이지만 마지막 frame age가 3초를 초과함
```

상단 요약은 단순 `ONLINE`이 아니라 이 조건을 모두 통과한 채널만 `HEALTHY`로 계산한다.

수동 확인 당시 결과:

```text
CH1~CH4 configured=YES
CH1~CH4 state=Playing
CH1~CH4 resolution=2592x1520
RTSP summary=4 / 4 HEALTHY
```

## 6. Live Logs

업무 이벤트를 표시하는 Events 페이지와 별도로 개발 진단 로그를 추가했다.

```text
Time | Level | Module | Code | Message
```

지원 기능:

- INFO, WARN, ERROR 필터
- API, RTSP, EVENT, SIMULATION 모듈 필터
- code/message 문자열 검색
- 자동 스크롤
- 현재 화면 로그 지우기
- 최대 1,000건 bounded 보관

실제 확인된 로그 예:

```text
ERROR | API   | API_STATE_CHANGED | RETRYING | Connection refused
INFO  | RTSP  | CH1_STATE_CHANGED | Playing
WARN  | RTSP  | CH1_STATE_CHANGED | Connecting
```

## 7. Test Tools 격리

기존 시뮬레이션 기능은 별도 탭으로 이동했다.

화면 상단에 다음 정책을 명시한다.

```text
Simulation sandbox:
these actions update only the local Qt state and are never sent to the Pi server.
```

유지된 기능:

- alarm ACK
- Mock EV 전환
- 비전기차 위반
- 장시간 주차 경고
- Hall sensor 오류
- 일반 주차 상태 randomize
- normalized RX sample
- manual normalized RX message

시뮬레이션이 실행되면 `ParkingSimulationService::simulationApplied` 신호가 발생하고 Debug 데이터 출처가 필요에 따라 `MOCK` 또는 `MIXED`로 변경된다.

## 8. 테스트

추가 테스트:

```text
tests/diagnosticsservice_test.cpp
```

검증 항목:

- URL credential과 query 마스킹
- 초기 Mock 적용 시 `MOCK`
- 서버 snapshot 적용 시 `SERVER`
- 서버 이후 시뮬레이션 적용 시 `MIXED`
- RTSP 채널 진단 저장
- 진단 로그 최대 1,000건 제한

최종 결과:

```text
[BUILD OK] smart_parking_qt_client.exe
[PASS] parkingcontroller_retry
[PASS] parking_simulation_service
[PASS] diagnostics_service
[PASS] camera_settings
[PASS] parkingmappage_add_slot
100% tests passed, 0 failed (5/5)
```

## 9. 실제 화면 검증

새 실행 파일을 실행하고 다음 항목을 직접 확인했다.

- Overview 카드 4개 표시
- `MOCK`와 `RETRYING` 상태 분리 표시
- API 연결 거부 오류와 retry 시간 갱신
- RTSP 4채널 `Playing`, 해상도, startup, frame age 표시
- Live Logs 실시간 추가
- Test Tools 별도 탭 표시
- 기존 Dashboard 영상 4채널 정상 재생

최종 재실행에서는 Pi API 응답도 확인됐다.

```text
Runtime data: SERVER
Pi API: CONNECTED
HTTP: 200
Applied server slots: 8
RTSP: 4 / 4 HEALTHY
```

따라서 동일 화면에서 연결 실패·재시도 상태와 정상 연결·동기화 상태 전환을 모두 확인했다.

## 10. 전체 연결 구조

```text
ParkingController
  -> apiDiagnosticChanged
  -> DiagnosticsService
  -> apiStateChanged
  -> DebugPage Overview

DashboardPage
  -> QML RTSP diagnostic property 수집
  -> 1초마다 rtspDiagnosticsChanged
  -> DiagnosticsService
  -> rtspChannelsChanged
  -> DebugPage Overview

ParkingSimulationService
  -> simulationApplied
  -> DiagnosticsService
  -> MOCK 또는 MIXED 출처 계산
  -> DebugPage Overview / Live Logs

ParkingController eventLogged
  -> DiagnosticsService ingestDomainEvent
  -> DiagnosticLogRecord
  -> DebugPage Live Logs
```

`MainWindow`는 모듈을 생성하고 signal/slot을 연결하지만 진단 상태 판정 자체를 담당하지 않는다.

## 11. Troubleshooting

### 11.1 화면 데이터와 실제 서버 데이터의 출처 혼동

기존 앱은 시작 시 Mock 데이터를 먼저 표시하므로 Pi가 연결되지 않아도 Parking Map과 Dashboard에 32개 슬롯이 보였다. 이 때문에 화면이 정상적으로 채워진 상태를 서버 연결 성공으로 오해할 수 있었다.

해결:

```text
초기 Mock만 적용       -> MOCK
서버 snapshot 적용     -> SERVER
서버 적용 후 로컬 조작 -> MIXED
아직 데이터 없음       -> UNKNOWN
```

API 상태와 데이터 출처를 서로 다른 카드에 표시해 두 상태를 분리했다.

### 11.2 RTSP `Playing`인데 과거 오류 문자열이 남는 문제

수동 QA에서 QML의 status는 `Playing`인데 `errorString`에 재연결 과정의 오류가 남는 순간이 확인됐다. status 문자열만 집계하면 이 채널을 정상으로 잘못 계산한다.

해결:

```text
Playing + error 없음 + frame age 3초 이하 -> PLAYING / HEALTHY
Playing + error 존재                      -> DEGRADED
Playing + frame age 3초 초과              -> STALE
그 외                                     -> 원래 연결 상태 표시
```

Overview 요약은 모든 configured 채널이 위 HEALTHY 조건을 충족할 때만 녹색으로 표시한다.

### 11.3 프레임 단위 진단 갱신 시 UI 부하 가능성

4개 채널의 약 30fps 프레임 신호를 Debug 표에 직접 연결하면 초당 약 120회의 QWidget 갱신이 발생할 수 있다.

해결:

- QML root에는 최신 진단 값만 property로 유지한다.
- `DashboardPage`가 1초마다 4채널의 최신 스냅샷을 수집한다.
- `DebugPage`의 frame age도 1초 주기로 다시 계산한다.

### 11.4 진단 URL에 credential이 노출될 가능성

RTSP 및 API 주소에는 사용자명, 비밀번호, query token이 포함될 수 있다. 원문 URL을 Overview나 로그에 표시하면 credential이 노출될 수 있다.

해결:

`DiagnosticsService::sanitizedUrl()`에서 user info, query, fragment를 제거한 주소만 저장·표시한다. 이 동작은 `diagnosticsservice_test`에서 검증한다.

### 11.5 Debug 동작이 운영 Controller를 오염시킬 위험

기존 버튼을 진단 화면과 함께 유지하면서도 시뮬레이션 코드를 Controller 내부로 되돌리지 않아야 했다.

해결:

- 시뮬레이션 버튼은 `Test Tools` 탭으로 격리한다.
- 버튼은 `ParkingSimulationService`만 호출한다.
- Pi 서버로 메시지를 전송하지 않는 local-only sandbox임을 화면에 명시한다.
- 서버 snapshot 이후 시뮬레이션을 실행하면 출처를 `MIXED`로 바꾼다.

## 12. 변경 파일과 책임

```text
src/diagnostics/diagnostictypes.h
  - API, RTSP, Parking, Log 진단 DTO

src/diagnostics/diagnosticsservice.h/.cpp
  - 진단 상태 집계
  - 데이터 출처 판정
  - URL sanitizing
  - bounded log 관리

src/pages/debugpage.h/.cpp
  - Overview / Live Logs / Test Tools UI
  - HEALTHY / DEGRADED / STALE 표시 판정

src/pages/dashboardpage.h/.cpp
  - 1초 주기 RTSP 진단 snapshot 발행

qml/RtspChannel.qml
  - RtspVideoItem의 최신 진단 property 노출

src/api/apiclient.h/.cpp
  - latency 및 HTTP status 결과 전달

src/controllers/parkingcontroller.h/.cpp
  - API 연결 상태와 retry telemetry 발행

src/simulation/parkingsimulationservice.h/.cpp
  - 시뮬레이션 적용 scenario signal 발행

src/mainwindow.h/.cpp
  - 각 모듈의 진단 signal/slot 연결

tests/diagnosticsservice_test.cpp
  - 데이터 출처, URL sanitizing, RTSP 저장, 로그 상한 검증
```

## 13. 현재 기준과 사용 방법

이후 네트워크 설정 개선으로 프로젝트 공용 Pi URL 기본값은 비활성화됐다. PC별 Pi 주소를 아직 저장하지 않은 현재 기본 실행 상태는 다음과 같다.

```text
Runtime data: MOCK
Pi API: DISABLED
RTSP streams: 카메라 로컬 설정에 따라 진단
Parking state: 초기 Mock 슬롯 표시
```

실제 Pi 진단을 확인하려면 Settings에서 전체 Server API URL을 저장한 뒤 Debug의 Overview에서 상태 전환을 확인한다.

운영 확인 순서:

```text
1. Overview에서 Runtime data 출처 확인
2. Pi API endpoint/state/retry/error 확인
3. RTSP 4채널 HEALTHY/DEGRADED/STALE 확인
4. 상세 이력이 필요하면 Live Logs 필터 사용
5. UI 동작 재현이 필요할 때만 Test Tools 사용
```

## 14. 다음 확장 후보

Pi 서버의 정규화된 health API가 정의되면 다음 상태를 Overview에 추가할 수 있다.

```text
Pi server
  ├─ MQTT broker/subscriber
  ├─ STM32 UART
  ├─ SQLite
  ├─ snapshot/clip storage
  ├─ OCR queue
  └─ TLS client session
```

Qt가 MQTT, UART, SQLite를 직접 점검하지 않고 Pi가 보고한 health snapshot을 표시하는 방향을 유지한다.

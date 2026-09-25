# Jira 등록 문구 - Debug 통합 진단 콘솔 개편

## 권장 이슈 제목

```text
[Qt Client] Debug 메뉴를 통합 진단 콘솔로 개편
```

## 이슈 유형

```text
Task
```

## 우선순위 제안

```text
High
```

이유: 기존 Debug 화면만으로는 화면에 표시된 데이터가 Mock인지 Pi 서버 응답인지 구분할 수 없고, API 재연결 상태와 채널별 RTSP 장애를 한 화면에서 확인할 수 없었다.

## Jira 본문 - 바로 붙여넣기

```text
h2. 배경

기존 Qt Client의 Debug 메뉴는 Mock 상태 변경과 수동 RX 문자열 주입 버튼만 제공했다.
화면에는 초기 Mock 데이터가 표시되기 때문에 Pi 서버 연결이 실패한 상황에서도 데이터가 정상 수신된 것으로 오해할 수 있었고, RTSP 특정 채널 정지나 API 재시도 상태를 확인하려면 여러 화면과 로그를 따로 확인해야 했다.

Debug 메뉴의 주 역할을 시뮬레이션 도구에서 통합 런타임 진단으로 변경한다.
기존 시뮬레이션 기능은 삭제하지 않고 별도 Test Tools 탭으로 격리한다.

h2. 목표

* 현재 주차 데이터 출처가 MOCK, SERVER, MIXED 중 무엇인지 표시
* Pi API 연결, HTTP, latency, retry, 오류 상태 표시
* CH1~CH4 RTSP 상태와 frame freshness 표시
* 운영 Events와 분리된 개발 진단 로그 제공
* 기존 시뮬레이션 기능을 local-only Test Tools로 유지

h2. 변경 사항

* Debug 메뉴를 Overview / Live Logs / Test Tools 3개 탭으로 개편
* DiagnosticsService와 진단 DTO 추가
* Runtime data source를 UNKNOWN / MOCK / SERVER / MIXED로 구분
* Pi API endpoint, 상태, latency, HTTP status, 마지막 성공 시각, 실패 횟수, retry, 오류 표시
* API endpoint 표시 전 credential, query, fragment 제거
* Dashboard QML에서 4채널 RTSP 진단 property 수집
* 1초 주기로 CH1~CH4 진단 snapshot 발행
* 채널별 configured, state, resolution, startup delay, frame age, last error 표시
* Playing 상태에서도 error가 남으면 DEGRADED, frame age가 3초를 넘으면 STALE로 표시
* 정상 조건을 모두 만족한 채널만 HEALTHY 집계
* Live Logs에 level/module filter, 검색, auto scroll, clear view 추가
* 진단 로그를 최대 1,000건으로 제한
* 기존 Mock/RX 기능을 Test Tools 탭으로 이동
* Test Tools가 Pi 서버로 메시지를 보내지 않는 local-only sandbox임을 명시
* 서버 상태 적용 후 시뮬레이션 실행 시 데이터 출처를 MIXED로 표시

h2. 모듈 구조

ParkingController -> API diagnostic -> DiagnosticsService -> Debug Overview
DashboardPage/QML -> RTSP snapshot -> DiagnosticsService -> Debug Overview
ParkingSimulationService -> scenario -> DiagnosticsService -> MOCK/MIXED 판정
ParkingController event -> DiagnosticsService -> Debug Live Logs

DiagnosticsService는 진단 정보를 집계할 뿐 ParkingController, RTSP 재생 또는 주차 상태를 직접 변경하지 않는다.

h2. Test Tools 유지 기능

* Acknowledge alarms
* Toggle mock EV
* Non-EV violation
* Overstay warning
* Hall sensor error
* Randomize parking
* Run normalized RX samples
* Manual normalized RX message injection

h2. 테스트 결과

* smart_parking_qt_client.exe 빌드 및 링크 성공
* parkingcontroller_retry PASS
* parking_simulation_service PASS
* diagnostics_service PASS
* camera_settings PASS
* parkingmappage_add_slot PASS
* CTest 5/5 통과, 실패 0건
* Overview, Live Logs, Test Tools 수동 UI 확인 완료
* API RETRYING/CONNECTED 전환과 HTTP 200 응답 확인
* CH1~CH4 2592x1520 Playing 및 4/4 HEALTHY 확인

h2. Troubleshooting

* 초기 Mock 32개 표시와 Pi 서버 연결 성공을 혼동하는 문제
** Runtime data와 Pi API를 별도 카드로 분리해 해결
* RTSP status가 Playing인데 과거 error가 남는 문제
** DEGRADED 상태를 추가하고 정상 집계에서 제외
* Playing 상태지만 새 frame이 들어오지 않는 문제
** frame age 3초 초과 시 STALE 처리
* 4채널 프레임 단위 UI 갱신 부하 가능성
** 최신 property만 유지하고 1초 snapshot polling 적용
* URL credential 노출 가능성
** user info, query, fragment sanitizing 적용 및 테스트 추가
* 시뮬레이션 코드가 운영 Controller로 다시 섞일 위험
** ParkingSimulationService와 Test Tools 경계를 유지

h2. 완료 조건

* Debug 메뉴에 Overview, Live Logs, Test Tools 탭이 표시되어야 함
* MOCK, SERVER, MIXED 데이터 출처를 구분해야 함
* Pi API 상태와 retry 원인을 확인할 수 있어야 함
* 4개 RTSP 채널의 상태와 frame age를 확인할 수 있어야 함
* error 또는 stale frame이 있는 Playing 채널을 HEALTHY로 계산하지 않아야 함
* 로그가 1,000건을 초과해 무한 증가하지 않아야 함
* Test Tools 동작이 Pi 서버로 전송되지 않아야 함
* 전체 CTest가 통과해야 함

h2. 현재 기본 상태 참고

네트워크 설정 개선 이후 공용 Pi 주소는 기본 비활성화 상태다.
PC별 Pi URL을 저장하지 않은 경우 Overview에는 Pi API DISABLED가 정상적으로 표시된다.
실제 Pi 연결 진단은 Settings에서 전체 Server API URL 저장 후 확인한다.

h2. 후속 작업

* Pi health API 정의 후 MQTT, UART, SQLite, storage, OCR queue 상태 추가
* 서버 정규화 event schema 확정 후 진단 code와 severity 정리
* 필요 시 Live Logs export 기능 추가
```

## 짧은 작업 완료 댓글

```text
Debug 메뉴를 단순 Mock 조작 화면에서 통합 진단 콘솔로 개편했습니다.

- Overview: Mock/Server 데이터 출처, Pi API, RTSP 4채널, 주차 상태
- Live Logs: level/module 필터, 검색, auto scroll, 최대 1,000건
- Test Tools: 기존 시뮬레이션 기능을 local-only 영역으로 격리
- RTSP: HEALTHY/DEGRADED/STALE 판정 추가
- 보안: API URL credential/query 제거 후 표시
- 검증: Qt Client 빌드 성공, CTest 5/5 PASS

현재 Pi 공용 주소는 비활성 기본값이므로, 실제 연결 진단은 Settings에서 PC별 Server API URL을 저장한 뒤 확인하면 됩니다.
```

## 권장 라벨

```text
qt-client
debug
diagnostics
observability
rtsp
raspberry-pi
simulation
```

## 관련 문서

```text
docs/2026-07-22_debug_diagnostics_console_worklog.md
docs/2026-07-22_qt_client_module_implementation_guide.md
```

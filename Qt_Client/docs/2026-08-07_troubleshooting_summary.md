# 스마트 주차 관제 시스템 트러블슈팅 정리 (2026-08-07)

## 1. 이슈 개요
* **현상**: Qt Client 화면에서 서버 연결이 된 것 같은데도 계속해서 재시도를 반복하는 현상 발생. 
* **특이사항**: Live Logs에 5초 간격으로 `CONNECTING` -> `CONNECTED` -> `API_SYNC (Applied 8 parking slots)` 로그가 무한 반복됨. 하단의 Overstay Policy 패널에서는 `HTTP 404: ENDPOINT_NOT_FOUND` 에러 노출.

## 2. 시스템 아키텍처 분석
문제 원인 파악을 위해 `Qt_Client` 및 `Pi_Server`의 통신 구조를 분석했습니다.

* **MQTT (포트 1883)**: 실시간 이벤트(차량 진입, 화재 알람 등) 수신 용도.
* **HTTP REST API (포트 8080)**: 프로그램 시작 시 초기 주차장 전체 상태(Snapshot) 동기화, 대용량 이미지 다운로드, 장기 주차(Overstay) 설정 동기화 용도. (`Pi_Server` 내부에 `cpp-httplib` 기반의 `ParkingHttpServer`가 구현되어 있음)

> [!NOTE]
> Qt Client는 MQTT 연결에 성공하면 초기 상태 동기화를 위해 즉시 HTTP API를 호출하도록 설계되어 있습니다. 만약 HTTP API 호출에 실패하면, 가짜 데이터(Mock Data)를 임시로 화면에 띄우고 5초 뒤에 HTTP API를 다시 호출하는 재시도 루프(`scheduleReconnect`)가 작동합니다.

## 3. 원인 추적 과정

### 1차 원인: 잘못된 포트 설정 (1883)
* **발견**: 사용자님의 설정(Settings) 화면 스크린샷에서 `API Port`가 `1883`으로 설정된 것을 확인했습니다.
* **분석**: Qt Client가 웹(HTTP) 요청을 MQTT 브로커(Mosquitto, 1883)로 보내고 있었습니다. 당연히 통신 규격이 맞지 않아 요청이 거절되었고, Qt Client는 통신 실패로 간주하여 Mock Data(8 slots)를 띄운 뒤 5초마다 재시도 루프를 돌고 있었습니다.
* **조치**: `API Port`를 `8080`으로 변경하도록 안내했습니다.

### 2차 원인: Pi_Server 구버전 실행 중
* **발견**: 포트를 `8080`으로 변경한 후에도 `HTTP 404: ENDPOINT_NOT_FOUND` 에러와 무한 재시도 현상이 동일하게 발생했습니다.
* **분석**: `404 ENDPOINT_NOT_FOUND`는 `Pi_Server`의 HTTP 서버가 "요청한 URL 경로가 존재하지 않는다"고 명시적으로 뱉어내는 커스텀 에러입니다. 
최근 `develop` 브랜치에 추가된 장기 주차 설정 API(`/api/v1/settings/overstay-threshold`) 등을 Qt Client가 요청하고 있지만, 현재 라즈베리 파이(`172.20.32.101`)에서 실제로 돌고 있는 `Pi_Server` 프로그램은 해당 API가 존재하지 않는 과거 버전(`main` 브랜치 등)이기 때문에 발생하는 에러입니다.

## 4. 최종 해결 방안 (Action Item)

> [!IMPORTANT]
> 원격 SSH 접속이 제한되어 있어 직접 조치해 드릴 수 없으므로, 사용자님께서 라즈베리 파이에 직접 접속하여 백엔드(Pi_Server)를 최신화하셔야 합니다.

1. 터미널(PuTTY 등)을 이용해 라즈베리 파이(`vedaproject@172.20.32.101`)에 SSH 접속
2. `Pi_Server` 디렉터리로 이동
3. `git checkout develop` 및 `git pull` 명령어로 최신 코드 동기화
4. `Pi_Server` 재빌드 및 프로그램 재실행

서버가 최신 버전으로 구동되면 8080 포트를 통한 정상적인 REST API 응답이 이루어지며, 5초마다 반복되던 렌더링 루프와 404 에러가 모두 해결될 것입니다.

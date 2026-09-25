# 2026-07-22 네트워크 주소 설정 개선 작업일지

## 1. 문제 확인

Settings 화면의 카메라 주소 입력부가 다음 형태로 구현되어 있었다.

```text
172.20.35. [마지막 숫자만 입력]
```

이 구조는 실제 장비가 항상 `172.20.35.0/24` 대역에 있다는 잘못된 가정을 코드와 UI에 고정한다. 기존 카메라 주소가 잘못되었을 때도 서비스 코드가 `172.20.35.0`을 임시 기준으로 사용하고 있어 다른 현장 대역으로 이동할 수 없었다.

Pi 서버 주소는 카메라와 달리 원래부터 전체 URL 입력 방식이다. 화면에 보인 `http://172.20.35.167:8080`은 코드 상수가 아니라 Git에 포함된 기본 설정 파일에서 읽은 값이었다. 하지만 새 PC에서도 해당 주소로 자동 접속을 시도하므로 배포 관점에서는 이 값 역시 제거해야 할 고정 주소 가정으로 판단했다.

## 2. 수정 원칙

```text
Camera
  -> 전체 IPv4 주소를 입력
  -> IPv4 형식을 검증
  -> 유효할 때만 camera_config.ini에 저장
  -> 저장 후 모든 RTSP 채널 URL을 다시 계산

Pi server
  -> 전체 API URL을 입력
  -> 프로젝트 기본값에서는 API 비활성 및 주소 미설정
  -> 전체 URL 저장 시 해당 PC의 local override에 API 활성 상태와 함께 저장
  -> 기존 reconnect 흐름 유지

공통
  -> 172.20.35 또는 다른 subnet/prefix를 코드에서 가정하지 않음
```

## 3. 코드 변경

### `src/pages/settingspage.*`

- `Edit last octet` 입력을 제거했다.
- 카메라 전체 IPv4 주소를 입력하는 `Camera IPv4 address` 필드를 추가했다.
- `172.20.35.` 고정 label을 제거했다.
- Pi 입력은 `Server API URL (full)`로 표시해 전체 URL 필드임을 명확히 했다.
- `Enter complete addresses. No subnet or IP prefix is assumed.` 안내 문구를 추가했다.

### `src/services/camerasettings.*`

- `saveLastOctet()`을 `saveCameraIp()`으로 교체했다.
- `QHostAddress`로 전체 IPv4 주소를 검증한다.
- 불완전한 주소, IPv6, 범위를 벗어난 주소, `0.0.0.0`, broadcast 주소를 저장하지 않는다.
- 잘못된 기존 주소를 발견해도 특정 subnet 주소로 자동 보정하지 않는다.

### `src/mainwindow.*`

- 전체 주소 저장 signal을 `MainWindow::saveCameraIp()`에 연결했다.
- 저장 성공 시 기존과 동일하게 4채널 RTSP URL을 다시 계산하여 Dashboard에 반영한다.

### `config/camera_config.example.ini`

- 실제 현장 대역처럼 보이던 주소를 문서용 IPv4 대역의 예시 주소로 교체했다.
- RTSP 주석은 `<camera-ip>` placeholder로 변경했다.
- 실제 credential과 로컬 설정 파일은 변경하거나 커밋하지 않는다.

### `config/client_config.ini`

- 특정 Pi 주소로 자동 접속하던 team-shared 기본값을 제거했다.
- 기본 상태는 `enabled=false`, `base_url=`이다.
- 사용자가 Settings에서 전체 URL을 저장하면 Git 제외 대상인 `client_config.local.ini`에 `enabled=true`와 주소가 기록된다.
- 따라서 저장소를 새 PC에서 실행해도 과거 Pi 주소로 자동 접속하지 않는다.

## 4. 테스트

`tests/camerasettings_test.cpp`를 추가했다.

검증 항목:

```text
[PASS 기대] 기존 192.168.1.x 설정에서 10.40.2.77 전체 주소 저장
[PASS 기대] 저장 결과에 과거 subnet prefix가 남지 않음
[PASS 기대] 172.20.35 같은 불완전한 주소 거부
[PASS 기대] 999.1.1.1 같은 범위 오류 거부
[PASS 기대] 잘못된 입력 뒤에도 마지막 정상 주소 유지
```

실제 검증 결과:

```text
[BUILD OK] smart_parking_qt_client.exe 컴파일 및 링크 성공
[PASS] parkingcontroller_retry
[PASS] parking_simulation_service
[PASS] diagnostics_service
[PASS] camera_settings
[PASS] parkingmappage_add_slot
100% tests passed, 0 failed
```

수정된 앱을 다시 실행하여 다음 UI 상태도 확인했다.

```text
Camera IPv4 address = 전체 주소 입력 필드
Server API URL (full) = 전체 URL 입력 필드
Network addressing = No subnet or IP prefix is assumed
공용 Pi 주소 미설정 상태 = API disabled
```

## 5. Troubleshooting

### 5.1 `QHostAddress`가 불완전한 IPv4를 허용한 문제

최초 구현에서는 `QHostAddress::setAddress()`만 사용해 IPv4를 검증했다. 그러나 테스트 결과 Qt가 다음 값을 유효한 IPv4의 축약 표기로 해석했다.

```text
입력: 172.20.35
예상: 형식 오류
실제: QHostAddress 검증 통과
```

Settings에서 축약 표기를 허용하면 사용자가 오타를 입력해도 다른 주소로 해석될 수 있다. 따라서 저장 전에 정규식으로 점으로 구분된 10진수 네 구간인지 확인하고, 그 다음 `QHostAddress`로 각 구간의 범위와 IPv4 protocol을 검증하도록 변경했다.

최종 검증 순서:

```text
입력 trim
  -> 점으로 구분된 4개 10진수 구간 확인
  -> QHostAddress IPv4 변환 확인
  -> 0.0.0.0 및 255.255.255.255 거부
  -> 정상 주소만 저장
```

### 5.2 새 테스트 타깃의 AutoMoc 실패

`camerasettings_test`를 처음 빌드할 때 MinGW의 AutoMoc compiler predefines 생성 단계에서 실패했다.

```text
AutoMoc subprocess error
moc_predefs.h generation failed
```

프로젝트의 기존 Qt 테스트 타깃과 동일하게 다음 속성을 적용해 해결했다.

```cmake
AUTOMOC_COMPILER_PREDEFINES OFF
```

이후 테스트 실행 파일 링크와 CTest가 정상 통과했다.

### 5.3 Pi 주소가 코드 상수는 아니지만 사실상 고정값이었던 문제

Pi URL은 전체 URL 입력 구조였으므로 처음에는 코드 하드코딩 문제가 아니라고 판단했다. 추가 확인 결과 Git에 포함된 `config/client_config.ini`가 다음 상태였다.

```text
enabled=true
base_url=http://172.20.35.167:8080
```

이 설정은 저장소를 새 PC에서 실행해도 과거 Pi 주소로 자동 연결을 시도하므로 배포 관점에서 사실상 고정 주소와 같다. 공용 기본 설정을 `enabled=false`, 빈 `base_url`로 변경하고, 사용자가 저장한 주소만 로컬 override에서 활성화하도록 수정했다.

## 6. 변경 파일

```text
CMakeLists.txt
config/camera_config.example.ini
config/client_config.ini
config/client_config.example.ini
config/client_config.local.example.ini
src/services/camerasettings.h
src/services/camerasettings.cpp
src/pages/settingspage.h
src/pages/settingspage.cpp
src/mainwindow.h
src/mainwindow.cpp
tests/camerasettings_test.cpp
tests/diagnosticsservice_test.cpp
docs/2026-07-22_qt_client_module_implementation_guide.md
docs/2026-07-22_network_address_configuration_worklog.md
```

실제 credential을 포함할 수 있는 `config/camera_config.ini`와 PC별 `config/client_config.local.ini`는 변경·커밋 대상에서 제외한다.

## 7. 현재 동작

```text
프로그램 시작
  -> 로컬 Pi 설정이 없으면 API disabled
  -> 과거 Pi 주소로 자동 접속하지 않음

Camera IPv4 저장
  -> 전체 IPv4 형식 검증
  -> camera_config.ini 갱신
  -> 4채널 RTSP URL 재계산

Server API URL 저장
  -> 전체 HTTP/HTTPS URL 검증
  -> client_config.local.ini에 enabled=true와 URL 저장
  -> API client 재생성 및 즉시 reconnect
```

## 8. 설정값 해석 시 주의사항

```text
현재 값이 172.20.35.x이다
  !=
프로그램이 172.20.35.x만 허용한다
```

실장비에서 현재 사용하는 주소는 각 PC의 로컬 설정값으로 존재할 수 있다. 중요한 기준은 새 네트워크로 이동할 때 카메라 전체 IPv4와 Pi 전체 API URL을 독립적으로 변경할 수 있어야 하며, 저장소의 공용 기본 설정이 과거 현장 주소로 자동 접속하지 않아야 한다는 점이다.

현재 Pi의 올바른 실제 IP가 확정되면 Settings의 전체 URL 필드에 다음 형식으로 입력한 뒤 `Save and reconnect`를 사용한다.

```text
http://<pi-ip>:<api-port>
```

TLS가 적용되면 서버 명세에 맞는 `https://` URL을 사용하며, 연결 실패 시 평문으로 자동 fallback하지 않는다.

## 9. 후속 작업

- 실제 Pi 서버 주소와 API port가 확정되면 각 테스트 PC의 Settings에서 전체 URL을 저장한다.
- 최종 서버 배포 단계에서는 `allow_insecure_http=false`와 HTTPS 인증서 정책을 적용한다.
- 카메라와 Pi가 DHCP 환경을 사용할 경우 고정 IP 하드코딩 대신 DHCP reservation 또는 hostname/mDNS 적용 여부를 별도 결정한다.
- 로컬 설정 파일과 실제 credential이 Git staged 상태에 포함되지 않는지 커밋 전에 다시 확인한다.

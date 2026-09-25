# Jira 등록 문구 - 카메라/Pi 고정 네트워크 대역 가정 제거

## 권장 이슈 제목

```text
[Qt Client] 카메라 및 Pi 서버의 고정 IP 대역 가정 제거
```

## 이슈 유형

```text
Task
```

## 우선순위 제안

```text
High
```

이유: 다른 네트워크 또는 다른 테스트 PC에서 실행할 때 과거 현장 주소로 자동 접속하거나 카메라 주소를 변경하지 못하는 문제가 발생한다.

## Jira 본문 - 바로 붙여넣기

```text
h2. 배경

Qt Client의 Settings 화면에서 카메라 IP가 172.20.35.x 대역으로 고정되어 있었고 마지막 octet만 변경할 수 있었다.
또한 Pi 서버 URL은 전체 URL 입력 방식이었지만, Git에 포함된 공용 client_config.ini에 특정 Pi 주소가 enabled=true 상태로 저장되어 새 PC에서도 해당 주소로 자동 연결을 시도했다.

현장 또는 테스트 네트워크가 변경될 경우 특정 subnet을 전제로 해서는 안 되므로 카메라와 Pi 주소 설정을 전체 주소 기반으로 변경한다.

h2. 문제점

* 카메라 주소 입력 UI에 172.20.35. prefix가 고정되어 있음
* CameraSettings가 현재 IP 형식 오류 시 172.20.35.0을 fallback으로 사용함
* Pi 공용 설정이 http://172.20.35.167:8080으로 활성화되어 있음
* 새 PC나 다른 네트워크에서 과거 Pi 주소로 자동 접속을 시도함
* 단순 QHostAddress 검증은 172.20.35 같은 축약 IPv4 표기를 허용함

h2. 변경 사항

* 카메라 입력을 마지막 octet 방식에서 전체 IPv4 입력 방식으로 변경
* Settings 화면의 172.20.35. 고정 label 제거
* CameraSettings::saveLastOctet()을 saveCameraIp()으로 변경
* 점으로 구분된 4개 10진수 구간과 IPv4 범위를 엄격하게 검증
* 잘못된 입력 시 기존 정상 설정 유지
* 카메라 IP 저장 성공 후 4채널 RTSP URL 재계산
* Pi 입력 필드를 Server API URL (full)로 명시
* 공용 client_config.ini를 enabled=false, base_url 미설정 상태로 변경
* 사용자가 저장한 Pi URL만 client_config.local.ini에 PC별 override로 저장
* 예제 설정에서 실제 현장처럼 보이는 고정 IP 제거
* camera settings 회귀 테스트 추가

h2. 최종 동작

* 로컬 Pi 설정이 없으면 API disabled 상태로 시작
* 저장소 기본값만으로 특정 Pi 주소에 자동 접속하지 않음
* 카메라는 유효한 전체 IPv4 주소만 저장 가능
* Pi는 subnet 제한 없이 전체 HTTP/HTTPS URL 입력 가능
* 실제 credential 및 PC별 로컬 설정은 Git에 포함하지 않음

h2. 테스트 결과

* smart_parking_qt_client.exe 빌드 및 링크 성공
* parkingcontroller_retry PASS
* parking_simulation_service PASS
* diagnostics_service PASS
* camera_settings PASS
* parkingmappage_add_slot PASS
* CTest 5/5 통과, 실패 0건
* 수정된 Settings UI 수동 확인 완료

h2. Troubleshooting

* QHostAddress가 172.20.35를 축약 IPv4로 허용하는 문제를 테스트에서 발견
* 정규식 기반 4구간 검증 후 QHostAddress 범위 검증을 수행하도록 보강
* camerasettings_test AutoMoc predefines 생성 실패 발생
* 기존 테스트와 동일하게 AUTOMOC_COMPILER_PREDEFINES OFF 적용 후 해결

h2. 완료 조건

* UI와 서비스 코드에 특정 subnet prefix가 없어야 함
* 카메라 전체 IPv4 주소를 저장하고 RTSP URL에 반영할 수 있어야 함
* 불완전하거나 범위를 벗어난 IPv4는 저장되지 않아야 함
* Pi 전체 API URL을 PC별 로컬 설정으로 저장할 수 있어야 함
* 로컬 설정이 없으면 특정 Pi 주소로 자동 접속하지 않아야 함
* 전체 CTest가 통과해야 함

h2. 후속 작업

* 실제 Pi 서버 IP 및 API port 확정 후 테스트 PC별 local 설정 적용
* 최종 배포 시 HTTPS 및 allow_insecure_http=false 적용
* DHCP 환경 사용 시 DHCP reservation 또는 hostname/mDNS 정책 검토
```

## 짧은 작업 완료 댓글

```text
카메라와 Pi 서버 설정에서 특정 IP 대역을 전제로 하던 부분을 제거했습니다.

- 카메라: 마지막 octet 입력 -> 전체 IPv4 입력 및 엄격한 형식 검증
- Pi 서버: 공용 고정 URL 제거, PC별 local 설정에 전체 URL 저장
- 기본 상태: Pi 주소 미설정 시 API disabled
- 테스트: Qt Client 빌드 성공, CTest 5/5 PASS

실제 Pi 주소와 API port가 확정되면 Settings에서 전체 URL을 저장해 연결하면 됩니다.
```

## 권장 라벨

```text
qt-client
network-config
configuration
bugfix
rtsp
raspberry-pi
```

## 관련 문서

```text
docs/2026-07-22_network_address_configuration_worklog.md
docs/2026-07-22_qt_client_module_implementation_guide.md
```

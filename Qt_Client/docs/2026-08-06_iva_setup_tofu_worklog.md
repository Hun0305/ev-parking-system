# 2026-08-06 업무 일지: IVA Setup 통합 및 TOFU(Trust On First Use) 구현

## 1. 개요 (Overview)
- **작업 목표**: `taejun/hanwha-iva-config` 브랜치의 IVA 설정(WiseAI Configuration API 연동) 기능을 `develop` 브랜치에 병합하고, 실사용 환경에서 발생하는 설정 누락 문제를 해결하여 팀원들의 개발 및 테스트 편의성을 향상시킴.
- **주요 성과**: 
  1. `develop` 브랜치에 IVA 설정 기능 PR (#13) 병합 및 단위 테스트 전체 통과 (28/28).
  2. 카메라 자체 서명 인증서(Self-signed Certificate)로 인한 연결 차단 문제를 해결하기 위해 Trust-On-First-Use (TOFU) 패턴을 적용한 자동 인증서 고정(Pinning) 기능 구현.

---

## 2. 장애 원인 분석 (Troubleshooting)

### 2.1. "IVA Setup 메뉴가 렌더링되지 않는 문제"
- **증상**: Qt Client를 빌드 후 실행했을 때, 메인 내비게이션에 `IVA Setup` 버튼은 존재하나 진입 시 카메라 데이터를 가져오지 못하고 오류 메시지가 노출되는 현상 발생.
- **원인**: 
  - WiseAI API는 HTTPS(Digest 인증) 기반으로 통신함. 
  - 한화 카메라의 내장 HTTPS 인증서가 자체 서명된 인증서(Self-signed Certificate)여서 QtNetwork 내부에서 `QSslError`를 발생시킴.
  - 기존 로직은 이를 방어하기 위해 `camera_config.ini` 파일에 `https_certificate_sha256` 해시값이 일치할 때만 예외를 허용하는 인증서 고정(Certificate Pinning) 방식을 사용함.
  - 하지만 팀원들이 깃에서 내려받는 `camera_config.example.ini` 기반의 로컬 설정에는 해당 해시값이 비어있어, API 요청 시마다 연결이 강제로 중단(Abort)됨.

---

## 3. 구조 변경 및 해결 (Design Changes)

### 3.1. TOFU (Trust On First Use) 패턴 도입
팀원들이 매번 카메라에 접속하여 인증서의 SHA-256 지문을 수동으로 추출해 설정 파일에 기입하는 번거로움을 없애기 위해, **최초 연결 시 인증서를 자동으로 신뢰하고 설정 파일에 기록하는 기능**을 구현함.

#### 변경된 컴포넌트:
1. **`WiseAiConfigClient` (`src/iva/wiseaiconfigclient.h/cpp`)**
   - `handleSslErrors` 로직 수정: `pinnedCertificateSha256` 값이 비어있을 경우 연결을 끊는 대신, 수신된 인증서의 SHA-256 해시값을 추출.
   - 추출한 해시값을 내부 옵션에 저장하고 `certificatePinned(const QString &sha256)` 신호(Signal)를 발생시킴.
   - 이후 에러를 무시(`reply->ignoreSslErrors(errors)`)하여 최초 연결을 정상적으로 허용함.

2. **`CameraSettings` (`src/services/camerasettings.h/cpp`)**
   - 영구 저장을 위해 `saveHttpsCertificateSha256(const QString &sha256, QString &errorMessage)` 함수 추가.
   - Qt의 `QSettings` 클래스를 활용하여 `camera_config.ini` 파일의 `[camera]` 섹션 내 `https_certificate_sha256` 키에 해시값을 기록.

3. **`MainWindow` (`src/mainwindow.cpp`)**
   - `WiseAiConfigClient` 객체 초기화 부분에서 `certificatePinned` 신호(Signal)를 수신.
   - 람다(Lambda) 함수를 통해 `m_cameraSettings.saveHttpsCertificateSha256(...)`를 호출하여, 최초 발견된 인증서 해시값을 파일에 자동 저장하도록 Signal-Slot 연결 구성.

---

## 4. 검증 (Verification)
- `cmake --build build` 를 통해 변경된 소스코드 빌드 완료.
- `ctest --test-dir build` 를 통해 28개의 단위 테스트 수행 완료.
- **결과**: `wiseai_config_client` 모듈을 포함한 100% (28/28) 테스트 성공.
- **영향**: 향후 프로젝트를 클론받는 팀원들은 `config/camera_config.example.ini`를 `config/camera_config.ini`로 복사하기만 하면, 애플리케이션 실행 시 IVA 메뉴에서 별도의 인증서 등록 절차 없이 매끄럽게 카메라 설정 연동이 가능해짐.

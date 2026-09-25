# CV Snapshot API 0.1.0 검증 계획

## 판정 원칙

정적 검토, CV5 Docker 빌드, CAP 생성, 카메라 런타임을 서로 다른 검증 단계로 기록한다. 한 단계의 성공으로 다른 단계를 통과로 기록하지 않는다.

## 1. 정적 계약 검토

- [x] `POST /startserver`, `GET /channels`, `GET /filters`, `POST /images/generate`, `GET /images/result/jpg` route와 문서 대조
- [x] `outputs` 1~7개, result ID 문자 규칙, 등록 필터 검증 경로 확인
- [x] 최근 8개 `run_id` cache와 `PROCESSING_BUSY` 정책 확인
- [x] `none`이 처리 실패가 아니라 원본 반환임을 문서와 UI 표기에서 확인
- [x] `snapshot_jpeg`, `display_image_opencv` 레퍼런스 무변경 확인

## 2. CV5 Docker 빌드와 CAP 생성

Linux 공유 폴더의 프로젝트 루트에서 실행한다.

```bash
APP_NAME=cv_snapshot_api SDK_VER=26.05.19 SOC=cv5 docker compose up
```

- [ ] CMake configure 성공
- [ ] AArch64 compile 및 link 성공
- [ ] `cv_snapshot_api.cap` 생성 시각 확인
- [ ] CAP에 최신 component, HTML, OpenCV library가 포함됐는지 확인

## 3. 카메라 운영 API 확인

- [ ] CAP 설치와 앱 시작
- [ ] `POST /startserver` 최초 요청 및 동일 포트 재요청
- [ ] `GET /channels` 실제 channel count와 default channel 확인
- [ ] `GET /filters` 활성 필터 5개 확인
- [ ] 채널별 기본 `/images/generate`에서 original·enhanced 생성
- [ ] 지정 필터 결과 생성
- [ ] 각 `results[].image_path`가 `image/jpeg`와 JPEG magic `FF D8 FF`를 반환
- [ ] 9번째 실행 후 가장 오래된 run만 404가 되는지 확인
- [ ] 잘못된 channel/filter/output과 Start Server 이전 요청 오류 확인

## 4. UI와 자원 확인

- [ ] 선택 채널별 Capture·Process·운영 이미지 세트 확인
- [ ] 4채널 전체 진단에서 채널별 결과가 서로 덮어쓰이지 않는지 확인
- [ ] 로그 조회·삭제와 접기 panel 확인
- [ ] 완료·실패·timeout 후 CPU, 메모리, thread가 정상 수준으로 복귀하는지 확인

## 5. 기록 방법

각 체크 항목에는 실행 날짜, CAP 파일명·생성 시각, 카메라 app ID, 채널, 요청·응답, 로그 위치를 남긴다. 실패 항목은 오류 코드와 재현 조건을 함께 기록한다.

`0.1.0`은 이 문서에서 아직 미체크인 최신 CAP·카메라 항목을 완료로 간주하지 않는다. 서버 OCR 및 STM LED 통합은 `0.2.0` 범위이며 이 검증 계획의 통과 조건에 포함하지 않는다.

## 6. 0.2.0 카메라 메타데이터 검증

- [x] source에서 선택적 `request_id` 파싱·길이·문자 검증 확인
- [x] source에서 성공 응답 `request_id` echo 확인
- [x] source에서 `captured_at_epoch_ms`와 `scene_assessment` 응답 생성 확인
- [x] `blackout`·`extreme_low_light`/IR/그 외 환경의 권고값 mapping 확인
- [x] request_id 미입력 시 기존 요청 형식과 응답 필드가 유지되는지 확인
- [ ] CV5 Docker build에서 새 source compile·link 확인
- [ ] 최신 CAP 설치 후 실제 카메라에서 새 JSON 필드 확인
- [ ] `INVALID_REQUEST_ID`와 서버·이미지 처리 오류 우선순위 확인
- [ ] 서버가 `request_id`와 `run_id`를 분리해 기록하는지 확인

카메라 앱은 LED 제어 API를 제공하지 않는다. 서버 OCR 및 STM LED 통합은 카메라 API
확장과 별도 검증 단계다.

## 7. 0.2.0 ROI 크롭 검증

- [x] `roi`의 `x,y,width,height` 및 `x1,y1,x2,y2` 정수 형식 parser 확인
- [x] 크롭된 frame이 환경 판단, original, auto, 지정 filter에 공통으로 전달되는지 확인
- [x] ROI가 이미지 경계 밖이면 `INVALID_ROI`와 HTTP 400으로 매핑되는지 확인
- [x] ROI 미입력 시 전체 frame 경로와 기존 요청 형식이 유지되는지 확인
- [x] 테스트 UI에서 ROI 적용 여부와 좌표 입력·응답 `roi` 표시 확인
- [ ] CV5 Docker build에서 ROI source compile·link 확인
- [ ] 최신 CAP·카메라에서 전체 frame과 ROI JPEG 크기·내용 비교
- [ ] 실제 frame 경계값, zero/negative 값, corner 형식의 `INVALID_ROI` 확인
- [ ] ROI 기준 환경 판단·조명 권고가 서버 OCR 정책과 맞는지 확인

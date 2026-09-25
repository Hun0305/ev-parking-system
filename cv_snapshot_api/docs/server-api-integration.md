# CV Snapshot API 백엔드 연동 가이드

- 문서 기준일: 2026-07-28
- 대상 앱: `cv_snapshot_api`
- 대상 SoC: CV5 / ARM64
- 구현 기준: 현재 `sdk/cv_snapshot_api` 소스

## 1. 문서 목적

외부 백엔드가 CV5 카메라에서 한 번 캡처한 동일 프레임의 다음 결과를 생성하고 조회하는
운영 API 계약을 정의한다.

- 원본 이미지
- 환경 자동 판단 후 개선된 이미지
- 백엔드가 지정한 필터 적용 이미지

환경 분석 점수, 개발 로그, Canny 진단 API는 운영 백엔드의 필수 연동 범위에 포함하지 않는다.

## 2. 구현 및 검증 상태

- 운영 API, 환경 판단, 활성 필터 5개와 웹 테스트 화면: 소스 구현 완료
- Windows 정적 검사와 API-소스 계약 대조: 통과
- CV5 Docker 빌드, 신규 CAP 생성·설치와 실제 카메라 호출: 검증 대기

따라서 현재 문서는 구현된 소스 계약을 설명하지만, 카메라 운영 검증 완료 증거는 아니다.
신규 CAP 검증이 끝나면 이 절의 상태와 실제 카메라 주소·앱 ID를 갱신한다.

## 3. 기본 주소

| 구분 | 형식 | 예시 |
|---|---|---|
| OpenAPI | `http://{camera-ip}/opensdk/{app_id}` | `http://172.20.35.216/opensdk/APP_ID` |
| 이미지 서버 | `http://{camera-ip}:{port}` | `http://172.20.35.216:8080` |

```text
OPEN_API_BASE=http://{camera-ip}/opensdk/{app_id}
IMAGE_BASE=http://{camera-ip}:{port}
```

OpenAPI 요청과 JPEG GET은 포트와 경로가 다르므로 백엔드는 두 base URL을 별도로 관리한다.

## 4. 운영 API 목록

| 순서 | Method | Endpoint | Base | 역할 |
|---:|---|---|---|---|
| 1 | POST | `/startserver` | `OPEN_API_BASE` | JPEG 조회용 TCP HTTP 서버 시작 |
| 2 | GET | `/channels` | `OPEN_API_BASE` | 지원 채널과 기본 채널 조회 |
| 3 | GET | `/filters` | `OPEN_API_BASE` | 직접 요청 가능한 검증 필터 조회 |
| 4 | POST | `/images/generate` | `OPEN_API_BASE` | 동일 프레임 결과 세트 생성 |
| 5 | GET | `/images/result/jpg?run_id={run_id}&result={id}` | `IMAGE_BASE` | 생성 JPEG 조회 |

백엔드는 `/channels`, `/filters` 응답을 기준으로 요청을 구성하고 채널 수나 필터 ID를
하드코딩하지 않는 방식을 권장한다.

## 5. 권장 연동 순서

```text
앱 연결 또는 설정 갱신 시
  POST {OPEN_API_BASE}/startserver
  GET  {OPEN_API_BASE}/channels
  GET  {OPEN_API_BASE}/filters

이미지가 필요할 때마다
  POST {OPEN_API_BASE}/images/generate
  응답의 run_id와 results[] 수신
  results[].image_path를 IMAGE_BASE에 연결
  결과별 JPEG를 즉시 GET
```

백엔드는 `run_id`를 미리 알 수 없으며 직접 생성해서도 안 된다. 반드시
`/images/generate` 응답의 `run_id`와 `results[].image_path`를 사용한다.

### 리소스 제어 (refector)

`/images/generate`는 CPU가 큰 OpenCV 처리이므로 전체 채널에서 동시에 하나만 실행된다.
기본 `processing_min_interval_ms`는 2,500 ms다. HTTP 503 `PROCESSING_BUSY` 또는 HTTP 429
`PROCESSING_RATE_LIMITED`를 받으면 즉시 재시도하지 말고, 429 `message`의 `retry after N ms`
이상 대기한 뒤 지수 backoff로 재시도한다. `outputs`를 생략한 기본 `original`·`auto` 두 장을
사용하고, 추가 필터는 실제로 필요한 경우에만 명시한다.

`GET /image/jpg`는 운영 이미지 세트 API가 아닌 호환·진단 경로다. 최근 Canny JPEG가 기본
2,500 ms cache TTL 안에 있으면 새 캡처 없이 반환하며, 처리 slot이 busy 또는 rate-limited일 때
이전 JPEG가 있으면 stale 결과를 반환한다. 연속 polling으로 새 캡처를 유발하면 안 된다.
세부 Attribute와 장비 검증 절차는 `resource-control.md`를 따른다.

## 6. 이미지 서버 시작

### 요청

```http
POST {OPEN_API_BASE}/startserver
Content-Type: application/json
```

```json
{
  "port": 8080
}
```

`port`는 1024~65535의 정수다. 숫자 문자열도 현재 구현에서 허용하지만 백엔드는 정수를 사용한다.

### 최초 정상 응답

HTTP 202는 시작 요청 접수를 의미하며 listener 준비 완료를 보장하지 않는다.

```json
{
  "success": true,
  "http_status": 202,
  "status": "accepted",
  "message": "server start requested",
  "port": 8080
}
```

같은 포트로 다시 요청하면 HTTP 200과 `server start was already requested`가 반환된다. 앱 실행
중 다른 포트로 요청하면 HTTP 409 `SERVER_ALREADY_CONFIGURED`가 반환된다. 포트를 변경하려면
앱을 재시작해야 한다.

최초 JPEG GET이 연결 거부되면 짧은 지연 후 제한된 횟수만 재시도한다.

## 7. 채널 조회

### 요청

```http
GET {OPEN_API_BASE}/channels
```

### 응답 예시

```json
{
  "success": true,
  "channel_count": 4,
  "default_channel": 0,
  "channels": [
    {"id": 0, "label": "CH 1"},
    {"id": 1, "label": "CH 2"},
    {"id": 2, "label": "CH 3"},
    {"id": 3, "label": "CH 4"}
  ]
}
```

현재 기본 설정은 4채널과 기본 채널 0이지만 카메라 설정에 따라 바뀔 수 있다.
`/images/generate`의 `channel`은 `0 <= channel < channel_count` 범위여야 한다. `channel`을
생략하면 `default_channel`이 사용된다.

## 8. 필터 조회

### 요청

```http
GET {OPEN_API_BASE}/filters
```

### 응답 예시

```json
{
  "success": true,
  "filters": [
    {
      "id": "fast_bilateral",
      "environment": "extreme_low_light",
      "validation_status": "synthetic_test_passed_field_pending"
    }
  ]
}
```

현재 직접 요청 가능한 필터는 다음 5개다.

| 필터 ID | 대응 환경 | 검증 상태 |
|---|---|---|
| `bilateral_d5` | `sensor_noise` | `csiq_test_passed` |
| `stretch_1_99` | `low_contrast` | `csiq_test_passed` |
| `fast_bilateral` | `extreme_low_light` | `synthetic_test_passed_field_pending` |
| `bilateral_gamma_clahe` | `ir_night` | `synthetic_test_passed_field_pending` |
| `backlight_combined` | `backlight` | `synthetic_test_passed_field_pending` |

`/filters`에는 `applies_processing=true`인 필터만 포함된다. `none`은 필터 ID가 아니므로
백엔드가 `type=filter` 요청에 넣으면 안 된다. 필터 목록은 앱 버전에 따라 달라질 수 있으므로
백엔드는 응답을 캐시하되 앱 재시작·업데이트 후 다시 조회한다.

## 9. 기본 이미지 생성

`outputs`를 생략하면 같은 프레임에서 `original`과 `enhanced` 두 결과를 생성한다.

### 요청

```http
POST {OPEN_API_BASE}/images/generate
Content-Type: application/json
```

```json
{
  "channel": 0
}
```

### CV 처리 환경 응답 예시

```json
{
  "success": true,
  "run_id": "img-15",
  "channel": 0,
  "detected_environment": "extreme_low_light",
  "auto_filter": "fast_bilateral",
  "image_server_port": 8080,
  "results": [
    {
      "id": "original",
      "type": "original",
      "applied_filter": "original",
      "jpeg_bytes": 249261,
      "processing_ms": 0.0,
      "image_path": "/images/result/jpg?run_id=img-15&result=original"
    },
    {
      "id": "enhanced",
      "type": "auto",
      "applied_filter": "fast_bilateral",
      "jpeg_bytes": 546036,
      "processing_ms": 182.4,
      "image_path": "/images/result/jpg?run_id=img-15&result=enhanced"
    }
  ]
}
```

### `none` 환경 응답 해석

환경은 판단됐지만 검증된 개선 필터가 없으면 다음과 같이 반환된다.

```json
{
  "detected_environment": "jpeg_artifact",
  "auto_filter": "none",
  "results": [
    {
      "id": "enhanced",
      "type": "auto",
      "applied_filter": "none",
      "image_path": "/images/result/jpg?run_id=img-16&result=enhanced"
    }
  ]
}
```

이 경우 `enhanced`는 별도의 CV 개선을 적용하지 않은 원본 내용의 JPEG다. 응답용 JPEG 인코딩은
수행되므로 `original`과 바이트 단위로 동일하다고 가정하지 않는다. 백엔드는 다음 기준으로
처리 여부를 판단한다.

```text
applied_filter == "none"     -> 개선 처리 없음
applied_filter == "original" -> 명시적 원본 결과
그 외 값                        -> 해당 필터 적용
```

`detected_environment`와 `auto_filter`는 표시·기록에 사용할 수 있다. 내부 환경 특징값은 운영
백엔드가 처리할 필요가 없다.

## 10. 지정 필터를 포함한 이미지 생성

모든 결과는 요청당 한 번 캡처한 동일 프레임을 사용한다.

```json
{
  "channel": 0,
  "outputs": [
    {"id": "original", "type": "original"},
    {"id": "enhanced", "type": "auto"},
    {"id": "low_light", "type": "filter", "filter": "fast_bilateral"},
    {"id": "contrast", "type": "filter", "filter": "stretch_1_99"}
  ]
}
```

| `type` | 필수 필드 | 결과 |
|---|---|---|
| `original` | `id` | CV 개선 없는 원본 내용 JPEG |
| `auto` | `id` | 자동 환경 판단에 따른 결과 JPEG |
| `filter` | `id`, `filter` | 지정 필터 적용 JPEG |

요청 제약:

- `outputs`는 1~7개다.
- `id`는 요청 안에서 고유해야 한다.
- `id`는 영문자, 숫자, `_`, `-`만 허용하며 최대 64자다.
- `filter`는 `/filters`가 반환한 ID만 사용한다.
- `type=original` 또는 `type=auto`에 `filter`를 보낼 필요가 없다.

## 11. JPEG 조회

최종 URL은 응답의 `image_path`를 사용한다.

```text
final_url = IMAGE_BASE + result.image_path
```

```text
http://172.20.35.216:8080/images/result/jpg?run_id=img-15&result=original
http://172.20.35.216:8080/images/result/jpg?run_id=img-15&result=enhanced
```

정상 응답:

```http
HTTP/1.1 200 OK
Content-Type: image/jpeg
Cache-Control: no-store
Access-Control-Allow-Origin: *
Connection: close
```

백엔드는 다음을 모두 확인한다.

- HTTP 200
- `Content-Type: image/jpeg`
- JPEG 시작 바이트 `FF D8 FF`
- 수신 크기가 `results[].jpeg_bytes`와 일치하는지 확인

## 12. 캐시와 동시 요청

- 카메라는 최근 8개 `run_id`의 결과를 메모리에 보관한다.
- 9번째 run 생성 시 가장 오래된 run이 제거된다.
- 앱 재시작 시 모든 run이 삭제된다.
- 생성 응답을 받은 즉시 해당 run의 모든 JPEG를 조회한다.
- 한 앱 인스턴스의 이미지 생성 요청은 순차 실행을 권장한다.
- 처리 중 새 생성 요청은 HTTP 503 `PROCESSING_BUSY`가 될 수 있다.

`PROCESSING_BUSY`는 짧은 지연 후 제한적으로 재시도한다. JPEG GET 404는 기존 run을 재사용하지
말고 `/images/generate`부터 다시 실행한다.

## 13. 오류 계약

### OpenAPI 오류 형식

```json
{
  "success": false,
  "http_status": 400,
  "status": "error",
  "stage": "image_generation",
  "error_code": "INVALID_FILTER",
  "message": "requested filter is not registered"
}
```

### 이미지 서버 오류 형식

```json
{
  "success": false,
  "http_status": 404,
  "stage": "image_generation",
  "error_code": "IMAGE_RUN_NOT_FOUND",
  "message": "image run was not found or expired"
}
```

### 주요 오류 코드

| HTTP | `error_code` | 발생 조건 | 백엔드 처리 |
|---:|---|---|---|
| 400 | `REQUEST_BODY_PARSE_ERROR` | JSON 객체가 아님 | 요청 수정, 재시도 금지 |
| 400 | `PORT_REQUIRED` | startserver에 port 없음 | port 추가 |
| 400 | `INVALID_PORT` | port 범위 오류 | 1024~65535로 수정 |
| 400 | `INVALID_CHANNEL` | 채널 형식·범위 오류 | `/channels` 기준으로 수정 |
| 400 | `INVALID_CHANNEL_CONFIGURATION` | 카메라 채널 설정 오류 | 앱 설정 확인 |
| 400 | `INVALID_IMAGE_OUTPUTS` | outputs가 배열이 아니거나 1~7개 범위 밖 | 요청 수정 |
| 400 | `INVALID_IMAGE_OUTPUT` | output의 id/type 형식 오류 | 요청 수정 |
| 400 | `INVALID_RESULT_ID` | id 규칙 위반 또는 중복 | ID 수정 |
| 400 | `FILTER_REQUIRED` | filter type에 filter 누락 | filter 추가 |
| 400 | `INVALID_FILTER` | 등록되지 않은 필터 | `/filters` 재조회 |
| 400 | `INVALID_OUTPUT_TYPE` | original/auto/filter 외 type | type 수정 |
| 400 | `IMAGE_RESULT_QUERY_REQUIRED` | JPEG GET query 누락 | 응답 image_path 사용 |
| 409 | `SERVER_ALREADY_CONFIGURED` | 실행 중 다른 포트 요청 | 앱 재시작 또는 기존 포트 사용 |
| 409 | `IMAGE_SERVER_NOT_STARTED` | 이미지 서버 시작 전 생성 요청 | `/startserver` 후 재시도 |
| 503 | `PROCESSING_BUSY` | 다른 프레임 처리 중 | 제한적 지연 재시도 |
| 502 | `SNAPSHOT_FAILED` | 현재 프레임 획득 실패 | 카메라 상태 확인 후 재시도 |
| 404 | `IMAGE_RUN_NOT_FOUND` | run 만료 또는 앱 재시작 | 생성부터 다시 실행 |
| 404 | `IMAGE_RESULT_NOT_FOUND` | 해당 result ID 없음 | 응답 image_path 확인 |
| 500 | `IMAGE_DECODE_FAILED` | 캡처 JPEG 디코딩 실패 | 카메라·앱 로그 확인 |
| 500 | `JPEG_ENCODE_FAILED` | 결과 JPEG 인코딩 실패 | 앱 로그 확인 |
| 500 | `OPENCV_FAILED` | OpenCV 예외 | 앱 로그 확인 |
| 500 | `IMAGE_GENERATION_FAILED` | 기타 생성 예외 | 앱 로그 확인 |

## 14. 타임아웃과 재시도 권장값

다음 값은 API 강제 규격이 아니라 백엔드 초기 권장값이다.

| 요청 | 초기 timeout | 재시도 |
|---|---:|---|
| `/startserver` | 10초 | 동일 포트 1~2회 |
| `/channels`, `/filters` | 10초 | 1~2회 |
| `/images/generate` | 30초 | `PROCESSING_BUSY`, 일시적 snapshot 실패만 제한적으로 |
| JPEG GET | 10초 | 같은 run이 유지되는 동안 1~2회 |

요청 timeout이 발생해도 카메라 내부 동기 처리가 즉시 취소된다고 가정하지 않는다. 연속 재요청으로
CPU 사용량을 높이지 않도록 지수 backoff 또는 고정된 짧은 대기와 최대 재시도 횟수를 둔다.

## 15. curl 예시

```bash
OPEN_API_BASE="http://CAMERA_IP/opensdk/APP_ID"
IMAGE_BASE="http://CAMERA_IP:8080"

curl -sS -X POST "$OPEN_API_BASE/startserver" \
  -H "Content-Type: application/json" \
  -d '{"port":8080}'

curl -sS "$OPEN_API_BASE/channels"
curl -sS "$OPEN_API_BASE/filters"

curl -sS -X POST "$OPEN_API_BASE/images/generate" \
  -H "Content-Type: application/json" \
  -d '{"channel":0}'

curl -f "$IMAGE_BASE/images/result/jpg?run_id=img-15&result=original" \
  -o original.jpg

curl -f "$IMAGE_BASE/images/result/jpg?run_id=img-15&result=enhanced" \
  -o enhanced.jpg
```

## 16. 백엔드 구현 의사 코드

```text
startImageServerOnce(configuredPort)
channels = GET OPEN_API_BASE/channels
filters  = GET OPEN_API_BASE/filters

assert requestedChannel exists in channels.channels
assert every requested filter exists in filters.filters

response = POST OPEN_API_BASE/images/generate {
  channel: requestedChannel,
  outputs: requestedOutputs
}

if response.error_code == PROCESSING_BUSY:
  boundedRetryWithDelay()

for result in response.results:
  jpeg = GET IMAGE_BASE + result.image_path
  verify HTTP 200
  verify Content-Type image/jpeg
  verify JPEG magic FF D8 FF
  verify byte length when available
  store or return jpeg using result.id
```

## 17. 운영 연동에서 사용하지 않는 진단 API

다음 API는 웹 개발·진단용이며 정상적인 백엔드 이미지 연동에는 필요하지 않다.

```text
POST /capture
POST /process
GET  /image/jpg
GET  /environment/catalog
POST /environment/analyze
POST /environment/test
GET  /environment/result/jpg
GET  /environment/jpg
GET  /logs
POST /logs/clear
```

진단 API를 운영 서버가 직접 의존하면 이후 변경 범위가 커지므로 운영 연동은 4절의 5개 API로
제한한다.

## 18. 백엔드 전달 전 확인 목록

- [ ] 신규 `cv_snapshot_api.cap` 빌드와 생성 시각 확인
- [ ] 카메라에 CAP 설치 후 앱 시작 확인
- [ ] 실제 `app_id`, 카메라 IP와 이미지 서버 port 확정
- [ ] `/channels` 실제 응답 저장
- [ ] `/filters`가 활성 필터 5개를 반환하는지 확인
- [ ] 기본 요청에서 original/enhanced JPEG 조회 확인
- [ ] `auto_filter=none` 케이스 처리 확인
- [ ] 지정 필터 결과 조회 확인
- [ ] 잘못된 channel/filter와 만료 run 오류 확인
- [ ] 처리 완료 후 CPU·메모리·thread 복귀 확인

## 19. 0.2.0 개발 중 메타데이터 확장

> 상태: 현재 source에 구현됨. CV5 Docker build, CAP 생성 및 실제 카메라 검증 전에는
> 운영 서버가 필수 항목으로 의존하지 않는다.

`POST /images/generate`의 method, path, 기존 요청·응답 필드는 유지한다. 다음 필드는
하위 호환 가능한 추가 항목이다.

### 요청

```json
{
  "request_id": "ocr-20260805-000123",
  "channel": 0,
  "outputs": [
    {"id": "original", "type": "original"},
    {"id": "enhanced", "type": "auto"}
  ]
}
```

- `request_id`는 선택 문자열이며 서버가 생성한다.
- 영문자, 숫자, `_`, `-`만 허용하고 최대 64자다.
- 생략하면 기존 0.1.0 요청과 동일하게 동작하며 응답에도 `request_id`를 넣지 않는다.
- 형식이 맞지 않으면 HTTP 400 `INVALID_REQUEST_ID`를 반환한다.

### ROI 크롭

`roi`도 선택 필드다. 지정하면 카메라는 캡처 후 ROI를 크롭하고, 크롭된 프레임으로 환경
판단·원본·자동 개선·지정 필터 JPEG를 모두 생성한다. 미지정 시 전체 프레임 동작을 유지한다.

```json
{
  "roi": {
    "x": 100,
    "y": 200,
    "width": 800,
    "height": 300
  }
}
```

아래 corner 형식도 허용하며 응답에서는 `x`, `y`, `width`, `height`로 정규화한다.

```json
{
  "roi": {
    "x1": 100,
    "y1": 200,
    "x2": 900,
    "y2": 500
  }
}
```

- 좌표는 캡처 JPEG의 원본 pixel 좌표다.
- `width`, `height`는 양의 정수여야 한다.
- `x`, `y`는 0 이상이어야 하며 ROI 전체가 이미지 내부에 있어야 한다.
- 형식·정수·범위가 잘못되면 HTTP 400 `INVALID_ROI`를 반환한다.

### 성공 응답 추가 필드

```json
{
  "request_id": "ocr-20260805-000123",
  "run_id": "img-21",
  "captured_at_epoch_ms": 1785901200123,
  "scene_assessment": {
    "lighting_condition": "extreme_low_light",
    "lighting_assist_recommendation": "recommended",
    "assessment_source": "image_heuristic",
    "assessment_version": "cv-scene-v1"
  },
  "roi": {
    "applied": true,
    "x": 100,
    "y": 200,
    "width": 800,
    "height": 300
  }
}
```

| 필드 | 의미 |
|---|---|
| `request_id` | 서버가 제공한 요청 상관관계 ID. 카메라가 생성하는 `run_id`와 다름 |
| `captured_at_epoch_ms` | `CaptureCurrentFrame` 성공 직후 기록한 UTC epoch milliseconds |
| `lighting_condition` | `detected_environment`와 같은 영상 기반 환경 이름 |
| `lighting_assist_recommendation` | `recommended`, `not_recommended`, `unknown` 중 하나 |
| `assessment_source` | 카메라 상태 API가 아닌 입력 이미지 특징 기반 판단임을 표시 |
| `assessment_version` | 영상 판단 규칙 버전. 현재 `cv-scene-v1` |

권고값은 LED 명령이 아니다. 서버가 OCR 결과, 재시도 제한 및 cooldown과 함께 최종 판단하고,
STM에 별도 ON/OFF 명령을 보낸다.

| 환경 | 권고값 |
|---|---|
| `blackout`, `extreme_low_light` | `recommended` |
| `ir_night`, `ir_reflection` | `unknown` |
| 나머지 환경 | `not_recommended` |

`ir_night`와 `ir_reflection`은 Day/Night, IR LED, IR-cut 상태를 카메라 앱이 알 수 없으므로
자동 점등 권고로 해석하면 안 된다.

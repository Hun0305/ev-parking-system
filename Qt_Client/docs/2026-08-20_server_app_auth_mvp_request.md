# Server 요청: Qt 앱 계정 인증 MVP

## 목적

Qt 관제 클라이언트용 독립 계정 체계를 Raspberry Pi 서버에 추가한다. 카메라 RTSP/WiseAI credential은 앱 계정으로 재사용·복사·seed·import하지 않으며, 새 DB schema와 설정에도 넣지 않는다.

이번 범위는 로그인 세션으로 기존 HTTP API와 증거 파일 접근을 보호하는 최소 구현이다.

## 확정 계약

- 저장소: 서버의 기존 SQLite DB, 신규 table은 `app_users`, `app_sessions` 두 개만 추가
- 비밀번호: libsodium의 `crypto_pwhash_str()` / `crypto_pwhash_str_verify()` 사용
  - 현재 기본 알고리즘인 **Argon2id**와 salt·parameter가 포함된 PHC 문자열만 DB에 저장한다.
  - `crypto_pwhash_OPSLIMIT_INTERACTIVE`와 `crypto_pwhash_MEMLIMIT_INTERACTIVE`를 시작점으로 사용하고, 실제 Pi에서 1회 검증 시간을 측정해 장비 여유 안에서 조정한다.
  - 최소 12자, 별도 대문자·숫자·특수문자 조합 규칙은 두지 않는다.
- account ID: trim 후 ASCII lowercase로 정규화하고 `^[a-z0-9][a-z0-9._-]{2,63}$`만 허용
- 세션 token:
  - libsodium `randombytes_buf()`로 생성한 32 random bytes를 base64url(no padding)로 한 번만 반환
  - DB에는 `SHA-256(raw_token)` 32-byte BLOB만 저장하고 raw token은 저장·로그하지 않음
  - 요청 검증 시 Bearer 값을 strict base64url decode한 뒤 raw 32 bytes인지 확인하고 같은 hash로 조회
  - `Authorization: Bearer <token>` header로만 전달; query string과 cookie는 사용하지 않음
- 수명: 로그인 시점 기준 absolute TTL, 기본 10시간(`36000`초)
  - 설정 허용 범위는 8~12시간(`28800`~`43200`초)
  - 요청 시 만료 연장 없음, refresh token 없음
- 전송: HTTPS만 허용. 인증서/key가 없거나 TLS 초기화가 실패하면 서버가 fail closed하며 HTTP로 fallback하거나 redirect하지 않음

## SQLite migration 예시

모든 timestamp는 UTC Unix seconds로 저장하고 DB 연결마다 foreign key를 활성화한다.

```sql
PRAGMA foreign_keys = ON;

CREATE TABLE app_users (
    user_id         INTEGER PRIMARY KEY,
    account_id      TEXT NOT NULL COLLATE NOCASE UNIQUE,
    password_hash   TEXT NOT NULL,
    enabled         INTEGER NOT NULL DEFAULT 1 CHECK (enabled IN (0, 1)),
    created_at_utc  INTEGER NOT NULL,
    updated_at_utc  INTEGER NOT NULL
);

CREATE TABLE app_sessions (
    session_id      INTEGER PRIMARY KEY,
    user_id         INTEGER NOT NULL,
    token_hash      BLOB NOT NULL UNIQUE CHECK (length(token_hash) = 32),
    created_at_utc  INTEGER NOT NULL,
    expires_at_utc  INTEGER NOT NULL,
    revoked_at_utc  INTEGER,
    FOREIGN KEY (user_id) REFERENCES app_users(user_id) ON DELETE CASCADE
);

CREATE INDEX idx_app_sessions_user_expiry
    ON app_sessions(user_id, expires_at_utc, revoked_at_utc);
```

- migration은 기존 주차·IVA·증거 data를 변경하지 않는 additive migration이어야 한다.
- `password_hash`에는 `$argon2id$...` PHC 문자열만, `token_hash`에는 digest만 들어가야 한다.
- 사용자 disable 또는 password reset은 같은 transaction에서 해당 사용자의 미만료 session을 모두 revoke한다.

## 설정 예시

```ini
[app_auth]
session_ttl_seconds=36000
login_window_seconds=300
login_max_failures=5
login_cooldown_seconds=60
```

TTL이 허용 범위를 벗어나거나 TLS 설정이 유효하지 않으면 조용히 기본값이나 plaintext로 내리지 말고 시작을 실패시킨다.

## HTTP API

### 공통 응답

기존 Qt client가 해석하는 오류 JSON 계약을 유지한다. 인증을 포함한 모든 HTTP 오류는 `Content-Type: application/json`과 다음 형태를 사용한다.

```json
{"success":false,"error":"authentication required"}
```

내부 DB 오류, account ID 존재 여부, disabled 여부, hash 검증 상세는 응답에 노출하지 않는다. 로그인·로그아웃 응답에는 `Cache-Control: no-store`를 설정한다.

### `POST /api/v1/auth/login`

계정 인증 없이 호출하는 로그인 endpoint다. 별도의 단순 생존 확인용
`GET /api/v1/health`도 공개 상태를 유지한다.

요청:

```json
{"accountId":"operator","password":"<user-entered-password>"}
```

성공 `200`:

```json
{
  "success": true,
  "accessToken": "<opaque-base64url-token-returned-once>",
  "tokenType": "Bearer",
  "expiresAt": "2026-08-20T21:00:00Z",
  "user": {
    "id": 1,
    "accountId": "operator"
  }
}
```

오류:

| Status | 조건 | `error` 예시 |
|---|---|---|
| `400` | JSON/field 형식 오류 | `invalid request` |
| `401` | 미존재 account ID, 틀린 password, disabled account | `invalid account or password` |
| `429` | 임시 rate limit | `too many login attempts; retry later` |
| `500` | DB/내부 오류 | `authentication service unavailable` |

미존재·오입력·disabled는 동일한 `401`과 메시지를 반환한다.

### `POST /api/v1/auth/logout`

유효한 Bearer token이 필요하다. 현재 session 하나의 `revoked_at_utc`만 기록한다.

성공 `200`:

```json
{"success":true}
```

누락·잘못됨·만료·이미 revoke된 token은 공통 `401`을 반환한다.

### 기존 endpoint 보호

`/api/v1/auth/login`과 단순 생존 확인용 `/api/v1/health`를 제외한 기존 endpoint 앞에 공통 auth middleware를 둔다. 최소 보호 대상은 다음과 같다.

- `/api/v1/parking-slots` 및 하위 상세/session endpoint
- `/api/v1/images/{imageId}` 등 snapshot·plate·clip·evidence 전송 endpoint
- `/api/v1/settings/overstay-threshold`
- `/api/v1/settings/parking-slots/roi` 및 하위 endpoint
- 그 밖의 기존 JSON/settings/evidence HTTP endpoint

증거 image/clip URL은 가능하면 같은 Pi origin의 상대 경로로 반환한다. Qt는 로그인한 origin과 scheme·host·port가 모두 같은 요청에만 Bearer token을 보낸다.

middleware는 token hash 조회와 함께 `revoked_at_utc IS NULL`, `expires_at_utc > now`, `app_users.enabled = 1`을 확인한다. 실패는 모두 다음 계약으로 통일한다.

```http
HTTP/1.1 401 Unauthorized
Content-Type: application/json
Cache-Control: no-store

{"success":false,"error":"authentication required"}
```

기존 endpoint의 성공 body는 변경하지 않는다.

## 임시 로그인 rate limit

영구 account lock은 구현하지 않는다.

- 서버 memory에 `(source IP, normalized account ID)`별 실패 시간을 bounded 상태로 유지
- 5분 내 5회 실패하면 60초 동안 `429`와 `Retry-After: 60` 반환
- 성공하면 해당 key의 실패 상태 제거
- 서버 재시작 시 상태가 사라져도 허용
- rate-limit 상태를 `app_users`에 저장하거나 관리자가 풀어야 하는 영구 lock을 만들지 않음

## 서버 관리 CLI

password를 command argument로 받지 말고 TTY에서 두 번 입력한다. stdout/log에 password, PHC hash, token hash를 출력하지 않는다.

```text
pi-server-admin app-user add <account-id>
pi-server-admin app-user list
pi-server-admin app-user disable <account-id>
pi-server-admin app-user reset-password <account-id>
```

- `add`: account ID 중복 검사, password 정책 확인, Argon2id hash 저장
- `list`: `user_id`, `account_id`, `enabled`, 생성·수정 시각만 표시
- `disable`: `enabled=0`과 활성 session 전체 revoke를 한 transaction으로 처리
- `reset-password`: 새 hash 저장과 활성 session 전체 revoke를 한 transaction으로 처리; disabled 상태는 자동 변경하지 않음
- 실패 시 non-zero exit code와 secret 없는 오류 메시지 반환

## 명시적 비범위

- refresh token, sliding expiration
- JWT
- OAuth/OIDC 및 외부 identity provider
- RBAC/권한 등급
- MFA
- 사용자 관리용 admin REST API와 self-signup
- remember-me와 client 장기 token 저장
- 범용 audit platform
- MQTT authentication 변경(별도 ticket)
- 카메라 계정 연동 또는 credential migration
- Python 구현·script·test

## 서버 전달물

1. 두 table의 schema migration
2. Argon2id password service와 opaque session service
3. login/logout route와 공통 HTTP auth middleware
4. `app-user` 관리 CLI
5. C++ 단위·통합 test와 설정 문서

## 구현 참고

- [libsodium password hashing](https://doc.libsodium.org/password_hashing/default_phf)
- [libsodium random data](https://doc.libsodium.org/generating_random_data)

프로세스 시작 시 `sodium_init()` 성공을 확인하고, password buffer는 검증 직후 지운다.

## Acceptance Criteria

- [ ] migration 후 기존 DB data가 유지되고 `app_users`, `app_sessions` 두 table만 인증 domain에 추가된다.
- [ ] CLI로 add/list/disable/reset-password가 동작하며 password나 hash가 shell history·stdout·log에 남지 않는다.
- [ ] 유효한 로그인은 매번 새로운 32-byte opaque token을 반환하고 DB에는 SHA-256 digest만 남긴다.
- [ ] password는 Argon2id PHC 문자열로만 저장되며 최소 12자 외 조합 규칙을 요구하지 않는다.
- [ ] 미존재·오입력·disabled account ID는 구분되지 않는 동일 `401` JSON을 반환한다.
- [ ] 5회 실패 rate limit은 일시적 `429`이며 영구 account lock을 만들지 않는다.
- [ ] logout 직후 해당 token으로 보호 endpoint에 접근하면 `401`이다.
- [ ] 만료 시각은 로그인 후 사용 여부와 무관하게 고정되며 refresh endpoint가 없다.
- [ ] disable/reset-password 직후 기존 모든 session이 무효화된다.
- [ ] 서버 재시작 후에도 만료·revoke되지 않은 SQLite session은 유지된다.
- [ ] login을 제외한 기존 JSON/settings/evidence endpoint가 Bearer token 없이 접근되지 않는다.
- [ ] 인증 오류가 모두 `{"success":false,"error":"..."}` JSON 계약을 지킨다.
- [ ] TLS 설정 실패 시 서버가 plaintext listener로 내려가지 않는다.
- [ ] 카메라 credential이 계정 seed, migration, fixture, 설정, log에 포함되지 않는다.
- [ ] 인증 관련 test와 기존 서버 회귀 test가 모두 통과한다.

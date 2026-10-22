# 018 봇이 실행마다 같은 캐릭터를 다시 써서 end-to-end가 이전 실행에 좌우됨

- 상태: 해결 (계정·캐릭터 이름을 행동 시드와 분리한 `bots.identity_seed`, 2026-10-22)
- 발견: 45 Zone 이전에서 로컬 end-to-end를 돌리다, 2026-10-22
- 영향: 같은 프로파일을 여러 번 돌리면 looters 판정이 실패했다. 체크포인트(45)가 생긴 뒤로는 봇이 스폰 지점이 아니라 지난 실행의 마지막 위치에서 시작해, 실행 결과가 데이터베이스에 남은 이전 실행에 따라 달라졌다

## 증상
`scripts/e2e-local.sh tools/loadgen/client/profiles/looters.toml`이 다음처럼 실패했다. 같은 코드로 몇 시간 전에는 통과했다.

```
pickups sent=38 acquired=0 (required 1) rejected=37 unknown=0 items_held=100 gold=491
FAIL: pickups were not recorded by the backend
```

## 원인 분석
- `items_held=100`은 looter 다섯이 20개씩 들고 있다는 뜻이고, 인벤토리 용량이 20이다. 획득이 모두 "인벤토리 가득"으로 거절됐다.
- 데이터베이스에서 확인하니 looter 캐릭터 다섯이 아이템 20개씩을 소유하고 있었다. 오늘 이전 실행들이 주운 것이다.
- loadgen은 계정 이름과 캐릭터 이름을 프로파일의 `seed`에서 만든다. 프로파일마다 시드가 고정이라, 실행할 때마다 같은 계정으로 로그인해 같은 캐릭터로 들어간다. 코드 주석은 이것을 의도로 적고 있었다("re-running a seed reuses its accounts").
- 계정 접두어만 바꿔 다시 돌리면 "캐릭터 이름이 이미 있다"로 실패했다. 캐릭터 이름도 같은 시드에서 나오기 때문이다. 시드를 바꿔 돌리자 통과했다(획득 9).

## 원인
ADR-0006은 "같은 시드는 같은 입력 시퀀스를 재생한다"를 요구한다. 그런데 구현은 캐릭터라는 **상태**까지 시드에 묶었다. 캐릭터에는 인벤토리, 골드, 처치 통계, 그리고 45부터는 마지막 위치가 쌓인다. 그래서 같은 입력을 보내도 실행마다 출발 상태가 달랐고, 쌓이다 보면 판정 조건(획득 1개 이상)을 만족할 수 없게 됐다.

## 해결
- 이름을 만드는 시드를 행동 시드와 분리했다. `LoadProfile::IdentityOf(prefix, identity_seed, index)`.
- `bots.identity_seed`는 선택 키다. 없으면 loadgen이 실행마다 OS에서 새로 받아 로그에 남긴다(`identity_seed=...`). 지난 실행의 계정을 일부러 다시 쓰려면 그 값을 프로파일이나 `SA_LOADGEN__BOTS__IDENTITY_SEED`로 고정한다.
- 봇의 입력은 그대로 `seed`에서 나온다. 같은 시드는 여전히 같은 입력을 보낸다.
- 대안: end-to-end 전에 봇 캐릭터의 아이템을 지우는 방법. 원장(economy_ledger)을 거치지 않고 자산을 지우는 것이라 버렸다. 실행마다 데이터베이스를 새로 만드는 방법은 개발 데이터까지 지운다.

## 재발 방지
- 테스트: 프로파일에 `identity_seed`가 없으면 비어 있다. 이름은 identity seed와 번호로 정해진다.
- 같은 프로파일의 end-to-end를 연달아 돌려도 통과해야 한다(45에서 looters를 연달아 두 번 돌려 확인).

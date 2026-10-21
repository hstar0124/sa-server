# 18 저장소 보호: 지워진 main을 되살리고 룰셋을 다시 시행한다

## 배경과 목표
0.2.0 릴리스 절차를 마치고 확인하다가 원격에서 `main`이 사라진 것을 발견했다([이슈 010](../issues/010-main-branch-deleted-after-the-back-merge.md)). 릴리스 표기를 `develop`에 되돌리는 PR은 머리가 `main`인데, 병합 직후 "Delete branch"로 `main`이 지워졌다. 그것을 막아야 할 룰셋은 시행 상태가 꺼져 있었다. 브랜치 복구와 룰셋 시행은 저장소 설정이라 코드 변경은 없다. 이 PR은 사고 기록과 절차 수정을 남기고, 다시 켠 룰셋 아래에서 병합되는 첫 PR이다.

## 고민한 선택지와 결정
| 주제 | 선택지 | 결정 |
|---|---|---|
| 되살릴 커밋 | `develop`의 머리 / 태그 `v0.2.0`의 커밋 | 태그 커밋(`0dbec6d`). `main`은 릴리스만 담는다. 되돌리기 PR은 `main`을 `develop`에 합쳤을 뿐 `main`을 바꾸지 않았다 |
| 되돌리기 방식 | `main` → `develop` 유지 / 릴리스 브랜치 → `develop` | `main` → `develop` 유지. `main`의 병합 커밋과 태그가 `develop`에서 닿아 버전 확인이 쉽다. 대신 머리 브랜치를 지우지 않는 규칙과 삭제 금지로 막는다 |
| 막는 장치 | 문서 규칙만 / 룰셋 삭제 금지 시행 | 둘 다. 사람은 같은 버튼을 다시 누를 수 있으니 저장소가 거절해야 한다 |

## 구현
- 저장소 설정(사용자 작업): `main`을 `0dbec6d`로 다시 만들고, 룰셋 `protect-main-branch`의 시행 상태를 Active로 바꿨다
- CONTRIBUTING.md: 룰셋이 거는 조건(PR 필수, CI 다섯 잡, merge commit만, 삭제·강제 푸시 금지)과 Active 상태. 릴리스 항목에 "되돌리기 PR 뒤 Delete branch를 누르지 않는다"와 마지막 확인 단계
- 이슈 010

## 만난 문제
- [010](../issues/010-main-branch-deleted-after-the-back-merge.md) 되돌리기 PR을 병합한 직후 `main` 브랜치가 지워짐. 룰셋이 꺼져 있어 삭제 금지가 동작하지 않았다.

## 검증
- 원격 `main`의 머리가 태그 `v0.2.0`의 커밋과 같다(`git ls-remote`).
- 룰셋 시행 상태가 `active`다(룰셋 API).
- 이 PR은 다시 켠 룰셋 아래에서 CI 다섯 잡을 통과해야 병합된다.

## 남긴 것
- 다음 릴리스(0.3.0)부터 CONTRIBUTING의 바뀐 릴리스 절차를 따른다.

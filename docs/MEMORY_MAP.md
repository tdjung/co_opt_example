# 메모리 맵 (제안)

플랫폼 IR이 다른 주소를 쓰면 `tools/tiers/*.json`의 `regions[].origin`과 `platform_regs.h`만 바꾼다.
링커 스크립트는 티어 JSON에서 자동 생성되므로 별도 수정이 없다.

| 영역 | 시작 | 크기 (Small/Medium/Large) | 접근 마스터 | 비고 |
|---|---|---|---|---|
| FLASH | 0x0000_0000 | 512K | CPU(I/D) | wait state 6/4/4, prefetch off/on/on |
| ITCM | 0x1000_0000 | 16K/32K/64K | CPU only | 코드 배치용 (`PLACE_CODE(REGION_ITCM)`) |
| DTCM | 0x2000_0000 | 16K/32K/64K | CPU only | 스택 여기. DMA/MAC 불가 |
| SRAM0 | 0x2400_0000 | 64K | CPU/DMA/MAC | 기본 .data/.bss |
| SRAM1 | 0x2401_0000 | 64K | CPU/DMA/MAC | Small: 있음 |
| SRAM2 | 0x2402_0000 | 64K | CPU/DMA/MAC | Medium 이상 |
| SRAM3 | 0x2403_0000 | 64K | CPU/DMA/MAC | Large |
| Peripherals | 0x4000_0000 | – | CPU | DMA 0x0000, Timer 0x1000, Audio 0x2000, Ctrl 0x3000, Log 0x4000, MAC 0x5000 |

## callgrind 영역 이벤트 주소 디코딩

코어 모델에서 접근 주소를 아래로 분류해 이벤트 카운터를 올린다 (`docs/HANDOFF.md` 2단계 계측):

| 이벤트 | 조건 |
|---|---|
| AccFlash | addr < 0x1000_0000 |
| AccTCM | 0x1000_0000 ≤ addr < 0x2400_0000 |
| AccSRAM0..3 | 0x2400_0000 + n×0x1_0000 구간 |
| AccPeriph | 0x4000_0000 이상 |

명령어 fetch도 같은 규칙으로 분류하면 `regions` 질의에서 코드 배치 효과가 보인다.

## 베이스라인 배치 (placement.h 기본값)

- 가중치 전부 Flash (in place) → conv/pw 커널이 wait state를 직접 맞음
- 활성화 A/B, NN 스크래치, MFCC 작업 버퍼, 특징 윈도우, **오디오 링버퍼** 전부 SRAM0 → DMA와 CPU가 같은 뱅크에서 경합
- 코드 전부 Flash, DTCM은 스택만
- 오프로드 없음, DMA burst 1, 더블 버퍼링

즉 Small 티어에서도 SRAM1이 비어 있고 DTCM이 놀고 있다. 여기서 시작해 AI가 배치·burst·오프로드를 찾아가게 한다.

## 용량 점검 (Small 티어)

| 객체 | 크기 | 후보 영역 |
|---|---|---|
| 가중치+파라미터 | ~29 KB (int8 22 KB + bias/mult/shift) | Flash / SRAM1 / DTCM(16K에 안 들어감 → 레이어 분할 필요) |
| 활성화 A/B | 8000 B × 2 | DTCM(둘 다 넣으면 16K 초과) / SRAM |
| NN 스크래치 | 4 KB | DTCM |
| MFCC 작업 | ~9 KB (float 버퍼) | DTCM / SRAM |
| 특징 윈도우 | 490 B | 어디든 |
| 오디오 링 | 1280 B × 2~3 | SRAM만 (DMA) |
| 스택 | ~4 KB | DTCM |

DTCM 16 KB에 "활성화 2개 + 스크래치 + 스택"이 들어가지 않으므로 무엇을 넣을지 선택해야 한다 — 이것이 의도된 knapsack 문제다.

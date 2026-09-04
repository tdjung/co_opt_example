# MAC 가속기 스펙 (INT8 dot-product engine)

신규 모델링 대상. 의도적으로 단순하게 설계했으며, CMSIS-NN의 정수 연산과 **bit-exact**해야 한다
(호스트 `hal_host.c`의 `hal_mac_dot_rows()`가 동작 레퍼런스).

## 1. 기능

한 번의 요청으로 `rows`개의 dot product를 계산한다.

```
for r in 0..rows-1:
    acc = acc_init ? acc_init[r] : 0
    for i in 0..len-1:
        acc += (int32)(A[i] + off_a) * (int32)(B[r*len + i] + off_b)
    result[r] = acc            # int32, wrap-around (오버플로 검출 없음)
```

- A, B: int8 배열, AHB 슬레이브 메모리(SRAM 뱅크, Flash)에 위치. **TCM 불가**.
- `acc_init`, `result`: int32 배열 포인터(SRAM). `acc_init == 0`이면 0에서 시작.
- 사용처: conv1(im2col, len=40, rows=64), pointwise conv(len=64, rows=64), FC(len=64, rows=12).

## 2. 레지스터 (제안 오프셋, `platform_regs.h`)

| 오프셋 | 이름 | R/W | 설명 |
|---|---|---|---|
| 0x00 | SRC_A | RW | A 주소 |
| 0x04 | SRC_B | RW | B 주소 (row-major, stride = LEN) |
| 0x08 | LEN | RW | 원소 수 (1..4096) |
| 0x0C | ROWS | RW | row 수 (1..64) |
| 0x10 | OFF_A | RW | int32 오프셋 |
| 0x14 | OFF_B | RW | int32 오프셋 |
| 0x18 | ACC_INIT | RW | int32[ROWS] 주소 또는 0 |
| 0x1C | RESULT | RW | int32[ROWS] 주소 |
| 0x20 | CTRL | W | bit0 START, bit1 IRQ_EN |
| 0x24 | STATUS | RW1C | bit0 BUSY(RO), bit1 DONE(W1C) |
| 0x28 | ID | RO | 0 = 미장착, 그 외 = lane 수 (8/16) |

START 시 BUSY=1. 완료 시 BUSY=0, DONE=1, IRQ_EN이면 인터럽트(IRQ2). START 중 재시작은 무시.

## 3. 타이밍 모델 (의도된 trade-off를 만드는 부분)

```
cycles = SETUP + rows * ( ceil(len / lanes) * BEAT + REQUANT_GAP )
       + 버스 대기 (A/B/RESULT 접근이 경합할 때 아비터가 결정)
```

- `SETUP` = 40 cycle (레지스터 래치·파이프라인 채움). 작은 레이어(FC: rows=12, len=64)는 오프로드가 CPU보다 느리도록 만드는 값. CPU CMSIS-NN은 대략 1 MAC당 0.5~1 cycle(DSP 확장 SIMD)이므로 FC 768 MAC ≈ 600~800 cycle vs 가속기 40 + 12×(4+2)=112 cycle → FC는 이득이 있고, 그 대신 rows=1 호출은 손해. PoC에서 손익 경계를 조정하려면 SETUP을 `tools/tiers/*.json`의 `mac.setup_cycles`로 바꾼다.
- `BEAT` = 1 cycle에 `lanes` 개 MAC. A는 한 번 읽어 내부 버퍼(최대 4096B)에 유지, B는 row마다 스트리밍.
- 데이터 읽기: 자체 AHB 마스터, 32bit(또는 버스 폭) 단위 burst. A 로드 = ceil(len/4) beat, B 로드 = rows × ceil(len/4) beat. 이 읽기가 CPU/DMA와 **같은 뱅크에서 경합**하면 지연되어야 한다 (뱅크 배정 최적화 축).
- `REQUANT_GAP` = 2 cycle (결과 쓰기).

## 4. 검증

- `tests/target_smoke/smoke.c` T5: 폴링/IRQ 두 모드 모두 CPU 레퍼런스와 비교.
- 펌웨어 전체: `run_experiment.py --knob OFFLOAD_PW1=1 ...` 결과가 골든과 bit-exact.

## 5. 면적/전력 (cost_table.json)

`area = mac_base + lanes × mac_per_lane`, `energy = mac_per_mac_op × MAC 수 + 버스 beat 에너지`, 누설 `mac_per_lane × cycles`.

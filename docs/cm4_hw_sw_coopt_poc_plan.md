# AI 기반 HW-SW Co-Optimization PoC 계획서
## Cortex-M4 가상 플랫폼 기반

> 이 문서는 새로운 채팅 세션에서 작업을 이어갈 수 있도록 지금까지의 검토 내용과 결정 사항을 정리한 것이다.
> 마지막 섹션(§11)에 새 세션 시작 시 필요한 입력물과 시작 프롬프트 예시가 있다.

---

## 1. 배경과 목표

### 1.1 문제 인식
- SoC/임베디드 개발에서 TAT가 지속적으로 줄어들어 실리콘 이전(pre-silicon) 단계에서 SW 개발을 시작하는 경우가 늘고 있다.
- HW-SW co-optimization 역시 pre-silicon 단계에서 진행하는 것이 유리하다.
- 기존 pre-silicon 수단의 한계:
  - RTL 시뮬레이션: 너무 느려서 SW 개발 불가
  - FPGA / 에뮬레이터(Zebu 등): 속도는 확보되나 분석 도구가 제한적
- 대안: cycle-level 분석이 가능한 코어 모델 기반 가상 플랫폼(1~10 MIPS)에서 SW 선개발 + co-optimization. 시뮬레이션 환경이므로 풍부한 분석 데이터와 도구 제공이 가능하다.

### 1.2 PoC 목표
사람이 플랫폼 위에서 직접 최적화하는 것이 아니라, **AI가 시뮬레이션 실행 → 분석 → HW/SW 수정 → 재실행 루프를 돌고, 개발자가 결과를 검토한 뒤 직접 또는 AI를 통해 다음 반복을 지시하는 환경**을 구성한다.

### 1.3 PoC 성공 조건
"AI가 최적화를 했다"가 아니라 **"사람이 며칠 걸릴 다차원 탐색을 AI가 하루에 돌고, 그 근거를 사람이 검토 가능한 형태로 제시했다"** 를 보여주는 것. 이를 위해:
- HW 변경에는 비용(면적/전력)이 따라야 한다 (성능만 보면 오도됨)
- SW 워크로드에 상충 관계가 심겨 있어야 한다 (최적점이 뻔하면 사람이 금방 함)
- 분석 데이터가 AI가 소비 가능한 구조여야 한다

---

## 2. 플랫폼 선택 검토 결과

### 2.1 gem5
- gem5 CPU 모델(AtomicSimple, TimingSimple, MinorCPU, O3, KVM)은 SystemC TLM이 아니라 gem5 고유의 Port/Packet 프로토콜 기반. TLM 연결은 `util/tlm` 브리지, 내장 SystemC 커널(`src/systemc`), Arm Fast Models 통합(`src/arch/arm/fastmodel`)으로 가능.
- MinorCPU/O3는 cycle-level(cycle-approximate)이며 cycle-accurate가 아님. ARM 기준 실측 대비 IPC 오차 1~17%(평균 7~8%), x86은 오차가 훨씬 큼.
- 속도: O3/Minor 약 0.1~0.5 MIPS, Atomic ~1 MIPS, KVM ~1000 MIPS.
- ISA: 코어 모델은 ISA 독립적으로 빌드되나 검증 성숙도는 ARM > RISC-V > x86 순.
- **gem5는 Cortex-M(M-profile)을 지원하지 않는다.** NVIC, SysTick, Thumb-only 등이 없어 임베디드 SoC PoC에 부적합.

### 2.2 GVSoC
- PULP/RISC-V 전용. 속도와 트레이스는 우수하나 CM4 생태계와 맞지 않음. RISC-V 2차 타깃 시 고려.

### 2.3 결정: 보유 CM4 모델 기반 자체 플랫폼 사용
이유: Cortex-M 지원, 이미 검증된 정합성, 기존 주변 모델 보유, 파라미터화 가능.

---

## 3. 보유 자산 현황 (확인된 사실)

| 항목 | 현황 |
|---|---|
| 코어 모델 | Cortex-M4, CoreMark 기준 cycle 정합성 97% 이상 |
| 시뮬레이션 속도 | 0.5 ~ 10 MIPS |
| 주변 모델 | DMA, Timer, Cache, Memory, Bus 보유. 추가 모델링 가능 |
| HW config | TCM, SRAM, Cache, memory latency, bus bandwidth 수정 가능. DMA burst는 추가 가능 |
| 버스 경합 | 경합 지연은 실제로 발생. 단, IP별 아비터 우선순위는 미적용 (경합 구간 cycle 오차 가능) |
| 플랫폼 구성 방식 | **JSON 형식 IR로 모델을 조합하여 SoC 플랫폼 구성** |
| 프로파일 데이터 | callgrind 형식. 이벤트: Ir / Cycle / Data Read / Data Write / Branch / Branch Miss / Cache Miss |
| 프로파일 UI | callgrind 기반 자체 현대적 UI 보유 (KCachegrind 대체) |
| 파형 | FST 형식. core: committed PC, func, insn, code line. bus: request addr, r/w data, size |
| 가속기 | 없음. 단순한 수준이면 신규 모델링 가능 |

미확인 사항:
- 버스 파형에 **마스터 ID 신호**가 있는지 (없으면 DMA/CPU 경합 구분 불가 → 추가 필요)
- 모델 소스 구조 (SystemC 여부, 신규 모델 추가 규약)
- JSON IR 스키마와 기존 예제 구성

---

## 4. HW 구조

### 4.1 토폴로지

```
CM4 ──(TCM I/F)── ITCM / DTCM      ← 버스 안 거침, DMA/MAC 접근 불가
 │
 ├─ M0 ─┐
DMA ─ M1 ─┤ AHB Matrix ├─ S0: SRAM bank0
MAC ─ M2 ─┘  (arbiter) ├─ S1: SRAM bank1   ← 뱅크마다 독립 슬레이브
                        ├─ S2: Flash (+prefetch)
                        ├─ S3: DMA regs
                        ├─ S4: MAC regs
                        ├─ S5: Timer
                        ├─ S6: Audio source (WAV → FIFO, DMA가 읽음)
                        └─ S7: Result / sensor / actuator regs
```

설계 원칙:
- **SRAM은 뱅크마다 별도 슬레이브**: 뱅크 배정이 경합 회피 최적화 축이 되도록
- **DMA, MAC은 마스터 + 레지스터 슬레이브 포트** 둘 다 필요
- **TCM은 DMA 접근 불가**로 결정 (실제 CM4 계열 CCM과 동일). "가중치를 TCM에 두려면 CPU가 복사해야 한다"는 제약이 배치 문제를 현실적으로 만든다
- 아비터 우선순위는 PoC에서 knob에서 제외하고 고정. 보고서에 오차 가능성 명시

### 4.2 HW knob와 탐색 범위

| 블록 | 베이스라인 (의도적으로 어중간하게) | 탐색 범위 | 비용 축 |
|---|---|---|---|
| CM4 | 100 MHz | 고정 | – |
| I-TCM / D-TCM | 16KB / 16KB | 8~64KB 각각 | area, leak |
| System SRAM | 2뱅크 × 64KB | 1~4뱅크, 총 128~256KB | area, leak |
| Flash | 6 wait state, prefetch off | 2~6 WS, prefetch on/off | prefetch buffer area |
| I-Cache | 없음 | 0/4/8/16KB, 2/4way | area, leak, E_access |
| AHB 매트릭스 | 32bit, fixed priority | 32/64bit | bus area |
| DMA | 2ch, burst 1 | 2~4ch, burst 1/4/8/16 | FIFO area |
| MAC 가속기 | 없음 | none / 8-lane / 16-lane | area(大), leak |

### 4.3 MAC 가속기 사양 (신규 모델링 대상, 최소 복잡도)
- INT8 dot-product 엔진. N-lane 병렬 MAC → 32bit 누산
- 메모리 맵 레지스터: src A 주소, src B 주소, 길이, 누산기 초기값, start, status/done, 결과
- 자체 AHB 마스터로 데이터 읽기 (DMA feed 또는 CPU 주소 전달 모두 가능하게)
- **셋업 오버헤드를 의도적으로 수십 cycle 부여**: 작은 레이어는 오프로드가 손해가 되도록 → 레이어별 판단이 필요해짐
- 완료 통지: 폴링 + 인터럽트 둘 다 지원

### 4.4 HW 티어 (개발자가 사전 정의, §7 참조)

| 티어 | 구성 |
|---|---|
| Small | TCM 16K+16K, SRAM 128K(2뱅크), cache 없음, MAC 없음 |
| Medium | TCM 32K+32K, SRAM 192K(3뱅크), I$ 4K, MAC 없음 |
| Large | TCM 64K+64K, SRAM 256K(4뱅크), I$ 16K, MAC 16-lane |

티어 내에서 AI가 바꿀 수 있는 것은 면적 중립적인 항목(뱅크 분할, DMA burst, Flash prefetch, 배치)으로 한정.

---

## 5. SW 워크로드

### 5.1 선정: Keyword Spotting(KWS) 파이프라인 + 제어 루프 ISR
후보 비교:
- CoreMark/Embench-IoT: 정합성 검증(Stage 0)에만 사용. 시스템 레벨 최적화 공간이 없음
- 오디오 필터/FFT: DMA·실시간은 좋으나 메모리 배치 문제가 작음
- **TinyML 추론(CMSIS-NN)**: 레이어별 연산/메모리 특성이 다르고 배치·오프로드·DMA 축이 풍부 → 채택. TFLite Micro는 코드가 커서 제외, CMSIS-NN 직접 호출

### 5.2 메인 워크로드: KWS

```
[DMA ch0] 16kHz PCM → 링버퍼(2×320 샘플, 20ms 프레임)
  → ISR: 프레임 완료 플래그
[main loop]
  → MFCC: 512-pt FFT(q15, CMSIS-DSP) → 멜필터 → DCT → 10 계수/프레임
  → 49프레임 × 10 특징 슬라이딩 윈도우
  → DS-CNN (CMSIS-NN INT8): conv 3×3 → DW-conv ×3 → pointwise ×3 → avgpool → FC(12 class)
  → argmax → 결과 레지스터 기록 + 로그
```

- 모델: Arm ML-Zoo DS-CNN KWS INT8 (tflite → C 배열 변환). 학습 불필요
- 12 클래스: yes/no/up/down/left/right/on/off/stop/go/silence/unknown
- 프레임당 약 2~3M 명령어 → 0.5~10 MIPS에서 0.3~6초

**메모리 footprint (상충 관계를 만들기 위한 의도적 설계)**
- 가중치 INT8 약 24KB → TCM 16KB에 안 들어감. Flash는 느리고, SRAM 복사는 부팅 시 DMA + 뱅크 선택 문제
- 활성화 버퍼 약 24KB (레이어별 ping-pong) → 가중치와 TCM 경쟁
- MFCC 작업 버퍼 약 4KB, 오디오 링버퍼 1.3KB → DMA가 쓰는 곳. CNN 버퍼와 같은 뱅크면 충돌
- 결과: 배치는 knapsack, 뱅크 배정은 경합 최소화, 오프로드는 레이어별 손익 판단 → 세 축이 얽힘

### 5.3 보조 워크로드: 1kHz 제어 루프 ISR
- 타이머 인터럽트 1ms마다: 센서 레지스터 읽기 → PID → 액추에이터 레지스터 쓰기
- **제약: 인터럽트 응답 지연 ≤ 20µs**
- 목적: KWS 최적화(예: 가속기 폴링)가 실시간성을 깨는지 검출, 단일 워크로드 과적합 방지

### 5.4 SW knob
- 링커 섹션 배치: 가중치 / 레이어별 활성화 / MFCC 버퍼 / 스택 → ITCM · DTCM · SRAM0~n · Flash
- 가중치 로딩 전략: Flash 직접 실행 / 부팅 시 DMA 복사 / 레이어 직전 DMA 프리페치
- 레이어별 MAC 오프로드 on/off, 오프로드 시 DMA feed vs CPU 주소 전달
- CMSIS-NN 커널 변형(DSP 확장 사용 여부), 버퍼 정렬
- 완료 대기: 폴링 vs 인터럽트
- DMA burst 크기, 더블/트리플 버퍼링
- 컴파일러 -O2/-O3/-Os, LTO

### 5.5 입력 / 출력

| | 입력 | 출력 | 사람 확인 | 자동 판정 |
|---|---|---|---|---|
| KWS | Google Speech Commands WAV (1s/16kHz/16bit, 폴더명 = 라벨) | top1 라벨 + 점수 + cycle 로그 | WAV 들어보고 라벨 비교 | 12개 logit **bit-exact** 골든 비교 |
| 제어 | 합성 스텝 신호 (센서 레지스터 모델) | 액추에이터 로그 | 스텝 응답 플롯 | 지연 제약 + 출력 시퀀스 골든 일치 |

- 테스트 벡터: 10~20개 WAV (클래스별 1~2개, 남/여 화자, silence, unknown 포함), 파일명-정답 표 고정
- 결과 로그 형식 예:
  ```
  [frame 49] t=1000ms  top1=yes (0.91)  top2=unknown (0.05)  cycles=2,314,552  deadline_margin=+0.4ms
  [ctrl] max_isr_latency=8.2us  missed=0
  ```
- 골든: 호스트(x86)에서 동일 CMSIS-NN 코드로 생성. 라벨 일치가 아닌 **logit bit-exact**로 판정해야 "정확도를 깎아서 빨라진" 경우를 잡는다
- 중간 산출물(디버그용): MFCC 특징(49×10) 스펙트로그램 이미지, 레이어별 활성화 체크섬
- 데모용: 클립을 이어 붙인 연속 스트림(silence + yes + stop + cat + silence)으로 시간축 검출 로그. 회귀는 단일 클립, 발표는 스트림
- 테스트 하네스: 매번 1초 윈도우를 채우지 말고 MFCC 버퍼를 미리 채운 상태에서 시작해 슬라이딩만 하도록 구성 (실행 시간 절감)

### 5.6 SW 계층 구조와 오픈소스 활용 범위

```
app/        kws_pipeline.c, control_isr.c   ← AI가 주로 편집 (배치, 오프로드, 버퍼링)
kernels/    CMSIS-DSP, CMSIS-NN              ← 오픈소스 그대로 (순수 C, HW 의존 없음)
            nn_offload.c                     ← CMSIS-NN 내부 dot-product를 MAC 호출로 바꾼 변형 + 레이어별 선택 테이블
hal/        dma.c, timer.c, mac_accel.c, audio.c   ← 모델 레지스터 맵에 맞게 직접 작성 (고정 API)
link/       linker.ld                        ← AI가 편집
startup/    벡터 테이블, 초기화
host/       골든 생성기 (같은 CMSIS 코드 x86 빌드), tflite→C 변환 스크립트
```

- 오픈소스가 제공하는 것: CMSIS-DSP/NN 커널, ML-Zoo 가중치, KWS glue 참고 코드(Arm ML-KWS-for-MCU)
- 직접 작성: HAL 전체, MAC 오프로드 래퍼, 링커/startup, 골든 생성기. 합계 약 1,500줄
- AI 편집 표면은 `app/`, `link/`, `nn_offload.c`의 선택 테이블로 한정. `hal/`은 고정 API (예: `dma_start(ch, src, dst, len, burst)`) → 레지스터 오조작 실패 방지, diff 가독성

---

## 6. 분석 데이터와 AI용 도구

### 6.1 callgrind 이벤트 보강 (비용 대비 효과 순)
현재 `Cycle − Ir`로 함수별 총 스톨은 나오나 원인은 모름.

1. **접근 영역 분류** (최우선, 가장 저렴): Data R/W와 명령어 fetch를 주소 디코딩으로 `AccTCM / AccSRAM0..n / AccFlash / AccPeriph` 로 분리 기록
2. **StallI / StallD**: fetch 대기 vs 데이터 대기
3. **BusWait**: 버스 grant 대기 cycle
4. StallPipe는 `Cycle − Ir − StallI − StallD`로 근사 가능, 불필요

1번만 있어도 Stage 1 시작 가능.

### 6.2 FST 파형 활용
- 원본 전체를 AI에 주지 않는다. callgrind로 병목 함수 → PC → 파형 검색 방식
- 오프라인 전처리로 **시간창(10µs)별 집계 테이블** 생성: 창마다 마스터별 요청 수, 슬레이브별 점유 cycle, 마스터 겹침 cycle, 실행 중 함수. 프레임당 수천 행이라 AI가 통째로 볼 수 있음
- 버스 파형에 마스터 ID 신호 필요 (미확인 → §3)

### 6.3 AI 질의 도구 세트 (callgrind·FST·UI 백엔드에서 파생, API 계층만 추가)

| 도구 | 기능 |
|---|---|
| `summary(exp)` | 총 cycle/에너지/면적, 데드라인 마진, 스톨 분해 비율, 영역별 접근 비율 |
| `top_functions(exp, event, n)` | 이벤트 기준 상위 n 함수 |
| `annotate(exp, function)` | 함수 소스 라인별 이벤트 |
| `memory_map(exp)` | 심볼별 배치 영역과 접근 수 |
| `bus_windows(exp, t0, t1)` | 시간창 집계 테이블 |
| `wave_at_pc(exp, pc, ±N)` | 해당 PC 전후 코어·버스 신호 |
| `wave_range(exp, t0, t1, signals)` | 좁은 창 원본 파형 |
| `diff(expA, expB)` | 함수별 이벤트 변화량 |

실행 제어 도구: `set_hw_config`, `build`, `run`, `verify_golden`, `edit_source`, `edit_linker`

---

## 7. 목적함수와 비용 모델

### 7.1 원칙
성능만 최적화하면 "캐시 최대, TCM 최대"로 수렴해 PoC가 무의미. HW 변경은 면적/전력 비용과 함께 평가해야 한다.

### 7.2 PoC 단계 방식: HW 티어 고정 표
에너지 모델 없이 시작. 개발자가 §4.4의 티어 3개를 사전 정의(이 시점에 면적/전력 판단을 이미 한 것). 결과는 아래 표로 제시 → 간이 Pareto front 역할.

| HW 티어 | 베이스라인 SW | AI SW-only | AI HW knob + SW (티어 내) |
|---|---|---|---|
| Small | | | |
| Medium | | | |
| Large | | | |

### 7.3 Pareto front (Stage 4 이후)
- 정의: 어떤 지표를 개선하려면 다른 지표를 반드시 희생해야 하는 해들의 집합. 가로축 면적(또는 에너지), 세로축 프레임당 cycle 산점도에서 왼쪽 아래 경계선
- 사람이 만든 설계가 front에서 얼마나 떨어져 있는지가 핵심 메시지

### 7.4 activity-based 에너지 모델 (Stage 4 이후)
```
Energy/frame = Σ_component (access_count × E_access) + Σ_component (P_leak × cycles)
Area         = Σ_component area(config)
Objective    = minimize Energy/frame
               s.t. latency ≤ deadline, Area ≤ budget, golden 통과, ISR 지연 ≤ 20µs
```
- access_count는 callgrind 영역별 접근 이벤트에서
- E_access / P_leak / area는 공개 자료(CACTI 계열, 40/28nm 논문)의 **상대값**. 예: TCM 접근 1, SRAM(버스 경유) 1.5~2, Flash 5~10, Cache hit 1.2, 가속기 MAC 1회 = CPU MAC 명령의 1/5
- 면적 예산 3단계로 바꿔가며 front를 그리는 방식이 안정적

---

## 8. AI 루프 구조와 Stage 계획

### 8.1 Stage
| Stage | 내용 | 목적 |
|---|---|---|
| 0 | CoreMark/Embench로 모델 정합성 확인, 분석 파이프라인 검증 | 신뢰 확보. 없으면 이후 결과를 아무도 안 믿음 |
| 1 | HW 고정, AI가 SW knob만 조정 | 기능 검증 자동화 완성. 사람 손 최적화와 비교 (첫 설득 자료) |
| 2 | SW 고정, 티어 내 HW 파라미터 탐색 | 단순 스윕 vs AI 탐색 비교 |
| 3 | 공동 최적화 | "HW를 바꾸면 SW 배치도 다시 짜야 한다"는 상호작용 |
| 4 | MAC 가속기 파티셔닝 + 에너지 모델 + Pareto front | 최종 발표 그림 |

### 8.2 운영 필수 요소
- 실험마다 git 브랜치/커밋 + config 해시 + 결과 JSON을 ledger에 기록 (재현성)
- 골든 불일치 또는 ISR 제약 위반 실험은 무효 (AI가 정확도를 깎아 cycle 줄이는 것 방지)
- 반복 예산, 면적 상한, 데드라인을 명시적 목적함수/제약으로
- 사람 검토 게이트: N회 반복마다 또는 HW 변경 제안 시 승인
- 결정론적 시뮬레이션(시드 고정)
- 비정상적으로 좋은 결과는 사람이 반드시 검토 (시뮬레이터 버그를 최적화했을 가능성)

### 8.3 실험당 시간 예산
- 실험 1회 = 부팅 + 워밍업 1프레임 + 측정 3~5프레임 ≈ 15M 명령어 → 최악 30초, 보통 수 초
- 하루 수백~수천 회 반복 가능

---

## 9. 작업 순서 (co-opt 루프 이전)

| 단계 | 작업 | 입력물 | 완료 기준 |
|---|---|---|---|
| 0 온보딩 | 플랫폼 문서·JSON IR 스키마·예제 구성·모델 소스 구조·빌드/실행 스크립트·테스트 펌웨어 파악. 기존 예제 그대로 빌드·실행 | 좌측 전부 | 기존 예제가 돌아가고 출력 위치·종료 조건·툴체인 확인됨 |
| 1 플랫폼 구성 | §4.1 토폴로지를 JSON IR로 구성 | IR 스키마, 기존 예제 | 테스트 펌웨어 부팅, 메모리 맵 디코딩 확인. IR로 표현 불가한 항목 → 2단계 범위 확정 |
| 2 HW 모델 | 신규: MAC 가속기, Audio source FIFO, 센서/액추에이터/결과 레지스터. 수정: DMA burst, Flash prefetch(필요 시). **계측: callgrind 영역 분류 이벤트(+StallI/D, BusWait), 파형 마스터 ID** | 모델 소스, 추가 규약 | 각 모델 단위 테스트 펌웨어 통과, callgrind에 새 이벤트 열 출력 |
| 3 SW 포팅 | HAL → 호스트 골든 생성기 → KWS 파이프라인 → 제어 ISR → MAC 오프로드 래퍼 → 링커 | 레지스터 맵, IRQ 번호, 테스트 펌웨어 | 테스트 WAV 10~20개 logit bit-exact, ISR 제약 통과, 프레임당 cycle 로그 = **베이스라인 수치** |
| 4 루프 인프라 | 회귀 스크립트(build→run→verify→collect), 실험 ledger, §6.3 질의 API, 티어 config 3개 | 3단계 산출물 | AI가 도구만으로 Stage 1 루프를 돌 수 있음 |

계측(2단계)을 SW 포팅 앞에 두는 이유: 이것도 모델 수정이며, 나중에 건드리면 재검증이 필요.

---

## 10. 리스크

| 리스크 | 대응 |
|---|---|
| 워크로드 과적합 | Stage 3부터 KWS + 제어 ISR 동시 목적함수. 필요 시 워크로드 추가 |
| AI가 시뮬레이터 버그를 최적화 | 비정상 결과 사람 검토 게이트. 골든 bit-exact |
| HW knob 무비용 팽창 | 티어 고정 → 이후 에너지/면적 모델 |
| 아비터 우선순위 미모델링 | 경합 구간 오차 가능성 보고서 명시. knob에서 제외 |
| 버스 파형에 마스터 ID 없음 | 2단계에서 신호 추가 |
| 분석 데이터 토큰 과다 | 집계 테이블 우선, 원본 파형은 좁은 창만 |

---

## 11. 새 세션 시작 가이드

### 11.1 작업 환경
- 시뮬레이터가 설치된 머신에서 **Claude Code**로 진행 권장. 포팅은 "작성 → 시뮬레이션 → 로그 → 수정" 반복이며, 이 환경이 곧 Stage 1 루프 인프라가 된다
- 채팅 컨테이너는 `gcc-arm-none-eabi` apt 설치와 소스 빌드는 가능하나 세션마다 초기화되고 시뮬레이터 실행에 별도 준비가 필요

### 11.2 시작에 필요한 입력물
0단계용 (우선):
1. JSON IR 스키마 문서 + 기존 SoC 구성 예제 파일
2. 모델 소스 트리 구조 설명 (SystemC 여부, 신규 모델 추가 규약, 빌드 방법)
3. 시뮬레이터 실행 방법: ELF 로드, config 지정, WAV 경로 지정, 종료 조건, callgrind/FST/로그 출력 경로
4. 기존 테스트 펌웨어 (부팅 + 간단 출력 수준이면 충분) + 빌드 스크립트 + 툴체인 버전

3단계용:
5. 메모리 맵, DMA/Timer/기타 레지스터 맵 (헤더 파일 가능), NVIC IRQ 번호
6. 세미호스팅 또는 UART 모델 지원 여부

### 11.3 시작 프롬프트 예시
```
첨부한 cm4_hw_sw_coopt_poc_plan.md 의 계획에 따라 작업한다.
현재 단계: §9 의 0단계(온보딩).
제공 파일: [IR 스키마], [예제 구성], [모델 소스 구조], [실행 스크립트], [테스트 펌웨어]
먼저 기존 예제를 빌드·실행하고, 확인된 사실과 계획서 대비 차이점을 정리한 뒤
1단계(JSON IR 플랫폼 구성)로 넘어간다.
```

### 11.4 진행 중 갱신할 항목
- §3 미확인 사항 → 확인 후 사실로 갱신
- §4.2 knob 중 IR 파라미터 vs 모델 수정 구분
- §5.2 실제 footprint 수치 (ML-Zoo 모델 변환 후)
- §9 각 단계 완료 시 베이스라인 수치 기록

---

## 부록 A. gem5 관련 참고 (질문 대응용)
- gem5 CPU 모델은 TLM이 아님. TLM 연결은 util/tlm 브리지, 내장 SystemC 커널, Fast Models 통합으로
- MinorCPU/O3: cycle-level, ARM 기준 실측 오차 평균 7~8% (1~17%), 속도 0.1~0.5 MIPS
- Cortex-M 미지원 → 본 PoC에 부적합
- ISA 성숙도: ARM > RISC-V > x86

# HANDOFF — 폐쇄망에서 남은 작업

이 리포지토리는 개방망에서 **플랫폼에 의존하지 않는 모든 것**을 완성해 둔 상태다. 폐쇄망에서는
아래 체크리스트만 순서대로 진행하면 Stage 1(SW-only AI 최적화 루프)에 들어갈 수 있다.
전체 배경·설계는 `docs/cm4_hw_sw_coopt_poc_plan.md`, Claude Code 지침은 `CLAUDE.md`.

## 완료된 것 (검증 포함)

| 항목 | 상태 |
|---|---|
| 펌웨어 app/kernels/hal.h, 호스트 HAL | 완료, x86 실행 검증 |
| DS-CNN Small (CMSIS-NN) | TFLite 인터프리터와 **bit-exact** (`model/verify_tflite_vs_cmsis.py`) |
| MFCC (CMSIS-DSP f32, libm-free) | TF audio_ops 대비 오차 < 0.006 |
| MAC 오프로드 커널 | CPU 경로와 bit-exact, 전 knob 조합 확인 |
| 타깃 startup / 링커 템플릿 / 타깃 HAL(제안 맵) / device 헤더 | arm-none-eabi-gcc 13.2 링크 성공 (`.text` 81 KB) |
| 스모크 테스트 펌웨어 | 빌드 성공 (`make -f target.mk SMOKE=1`) |
| callgrind 파서·질의 CLI | 픽스처로 테스트 |
| 하네스(run/verify/ledger/tier/linker gen/cost) | 호스트 백엔드로 end-to-end 검증 |
| 골든 (합성 오디오 7클립, 난수 가중치) | `data/golden/*.log` |

## 개방망에서 사용자가 추가할 데이터 (LFS/데이터셋 차단으로 미포함)

- [ ] `data/model/ds_cnn_s_quantized.tflite` — Arm ML-zoo `models/keyword_spotting/ds_cnn_small/model_package_tf/model_archive/TFLite/tflite_int8/`
- [ ] `data/model/testing_input/`, `testing_output/` (같은 위치의 npy)
- [ ] `data/audio/<label>/*.wav` — Google Speech Commands 10~20개 (yes/no/up/down/left/right/on/off/stop/go + unknown용 단어 2~3개 + `_background_noise_` 1개)

추가 후 개방망 또는 폐쇄망에서:
```
python3 model/gen_weights.py --tflite data/model/ds_cnn_s_quantized.tflite   # generated/ 갱신
python3 tools/harness/make_golden.py                                        # 골든 재생성
git commit -am "Real weights + goldens"
```

## 폐쇄망 작업 순서

### 0. 온보딩 (반나절)
- [ ] 플랫폼 IR 스키마·예제 구성 읽기, 기존 예제 빌드·실행
- [ ] 다음을 확인해 `docs/HAL_SPEC.md` 대비 차이를 메모: 레지스터 맵, IRQ 번호, DWT 지원 여부, 로그 출력 방식(UART 모델/세미호스팅), 종료 방법, callgrind/FST 출력 경로, WAV 입력 방법
- [ ] 버스 파형에 **마스터 ID** 신호가 있는지 확인 (없으면 2단계에서 추가)

### 1. 플랫폼 IR 구성 (1일)
- [ ] `tools/tiers/small.json`의 토폴로지를 IR로 작성 → `tools/tiers/small.json`의 `platform_ir` 경로 채우기 (medium/large도)
- [ ] 필요 시 `tools/harness/gen_platform_ir.py`를 작성해 티어 JSON → IR 자동 변환 (IR 스키마를 알면 30줄 수준)
- [ ] `platform_regs.h`를 실제 주소/비트로 교체
- [ ] `tools/harness/sim_run.py::run_target()` 구현 (시뮬레이터 호출 한 줄)
- 완료 기준: `make -f target.mk SMOKE=1` ELF가 부팅하고 `SMOKE start` 로그가 나옴

### 2. HW 모델 (2~4일)
- [ ] Audio source FIFO 모델 (WAV → FIFO, EOF)
- [ ] 센서/액추에이터/결과/마커/종료 레지스터 블록 (플랜트 식은 `HAL_SPEC.md`)
- [ ] MAC 가속기 (`docs/MAC_ACCEL_SPEC.md`) — Large 티어에만 장착, ID 레지스터로 존재 여부 노출
- [ ] DMA: burst 파라미터, SRC_FIX 모드 (없으면)
- [ ] Flash prefetch 버퍼 (없으면; Medium/Large 티어)
- [ ] **계측**: callgrind `events:`에 `AccTCM AccSRAM0..3 AccFlash AccPeriph` 추가 (주소 디코딩은 `MEMORY_MAP.md`). 가능하면 `StallI StallD BusWait`
- [ ] 파형: 버스 마스터 ID, TRACE_MARKER 값
- 완료 기준: 스모크 테스트 `SMOKE done pass=N fail=0` (Large 티어에서 T5 포함). callgrind 헤더에 새 이벤트 열

### 3. 펌웨어 타깃 실행 (1~2일)
- [ ] `python3 tools/harness/run_experiment.py --name base_small --tier small --backend target --clips data/audio_synth/two_tone.wav`
- [ ] `verify_ok: true` 확인. 실패 시:
  - MFCC 프레임 불일치 → float 결정성 문제. `-ffp-contract=off` 확인, `mfcc_last_float` 덤프 비교. 최후 수단: 타깃 첫 실행 로그를 골든으로 채택(`make_golden.py`가 아니라 수동 복사) 후 이후 실험은 그 골든과 비교
  - logits 불일치, MFCC 일치 → `cksum` 첫 불일치 레이어로 좁힘. CMSIS-NN DSP 경로 vs 호스트 경로 차이면 `-DARM_MATH_DSP` 제거로 확인
  - CTRL seq_cksum 불일치 → 플랜트 갱신/ISR 순서 또는 setpoint 시각
- [ ] 세 티어 모두 베이스라인 실행, `experiments/ledger.jsonl`에 기록 → **베이스라인 수치**
- 완료 기준: 3 티어 × 합성 클립 golden 통과, 추론당 cycle·ISR 지연 수치 확보

### 4. 루프 인프라 마무리 (1일)
- [ ] `tools/callgrind/cg_query.py`가 실제 callgrind 파일을 읽는지 확인 (형식 차이 시 `callgrind.py` 조정)
- [ ] FST 집계: `tools/fst/bus_windows.py` 작성 — 10 µs 창별 마스터×슬레이브 요청 수/점유/겹침 → CSV. (파형 포맷·신호명을 알아야 해서 여기서는 미작성. `pyfst`/`fstapi` 또는 `fst2vcd` 후 파싱)
- [ ] `wave_at_pc(pc, ±N)`: committed PC 신호에서 PC 매칭 → 시간창 → 버스 신호 추출
- 완료 기준: `CLAUDE.md`의 도구 표에 있는 명령이 전부 동작

### 5. Stage 1 시작
- `CLAUDE.md`의 "최적화 루프 지침"으로 새 세션 시작. 첫 목표: Small 티어에서 `avg_infer_cycles` 최소화, 제약 golden 통과 + ISR ≤ 20 µs + overruns 0.

## 실험당 시간 예산 (참고)
추론 1회 ≈ 2.7 M MAC ≈ 6~10 M cycle(추정). 1초 클립 = 프레임 50 + 추론 1회 ≈ 12 M 명령어 → 0.5~10 MIPS에서 1~25초.
스트림 3초 클립은 추론 30회라 회귀에는 1초 클립을, 데모에만 스트림을 쓴다.

## 알려진 제약 / 결정 사항
- 아비터 우선순위 미모델링: 경합 구간 cycle 오차 가능. 보고서에 명시.
- `KWS_INFER_HOP_FRAMES`는 스펙(5 = 100 ms)이며 최적화 knob이 아님(바꾸면 골든 재생성).
- 호스트 골든의 cycle 필드는 0 (호스트는 기능 검증 전용).
- `CMSIS_NN_USE_SINGLE_ROUNDING`이 호스트/타깃 공통 기본값. TFLite와 일치시키기 위함이며 빼면 안 됨.

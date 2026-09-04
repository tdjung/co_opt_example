# CLAUDE.md — 이 리포지토리에서 작업하는 Claude를 위한 지침

## 무엇을 하는 프로젝트인가
Cortex-M4 가상 플랫폼 위에서 KWS(TinyML) + 제어 루프 펌웨어를 대상으로 **AI가 HW-SW co-optimization
루프를 돌고 사람이 검토**하는 PoC. 배경과 설계 결정은 `docs/cm4_hw_sw_coopt_poc_plan.md`,
남은 작업은 `HANDOFF.md`. 지금 어느 단계인지는 사용자가 알려준다.

## 리포지토리 지도
```
firmware/app/        KWS 파이프라인, MFCC, DS-CNN, 제어 ISR, main  ← 최적화 대상 (placement.h가 knob)
firmware/kernels/    MAC 오프로드 변형 (CMSIS-NN과 bit-exact)
firmware/hal/hal.h   고정 HAL API — 절대 수정 금지
firmware/hal/host/   x86 골든 생성용 HAL
firmware/hal/target/ 타깃 HAL + platform_regs.h(레지스터 맵) + device_cm4.h
firmware/startup, link/  CM4 startup, 링커 템플릿(티어별 생성)
firmware/third_party/    CMSIS-DSP/NN/Core (벤더링, 수정 금지)
model/               가중치 생성기(gen_weights.py), MFCC 테이블, tflite 검증
host/                호스트 빌드(Makefile), nn_only
tools/harness/       run_experiment, verify_golden, make_golden, gen_linker, sim_run(플랫폼 어댑터), parse_log
tools/callgrind/     callgrind 파서 + cg_query
tools/tiers/         HW 티어 small/medium/large
tools/cost/          면적·에너지 상대 비용
tests/               타깃 스모크 펌웨어, callgrind 픽스처
data/                golden 로그, 합성 오디오, (사용자 추가) 모델·실음성
experiments/         실험 결과 + ledger.jsonl (git 제외)
```

## 절대 규칙
1. `hal.h`, `third_party/`, `app/main.c`의 로그 포맷은 수정하지 않는다. 로그 포맷을 바꾸면 `tools/harness/parse_log.py`와 골든이 깨진다.
2. **골든 불일치 = 실험 무효.** 정확도를 깎아 cycle을 줄이는 것은 최적화가 아니다. `verify_ok: false`인 결과를 개선이라고 보고하지 않는다.
3. 실험은 소스를 고치지 않고 `run_experiment.py --knob`으로 한다. 소스 수정(새 knob 추가, 알고리즘 변경)은 별도 커밋으로 분리하고, 알고리즘 변경 시 `make_golden.py`로 골든을 재생성한 뒤 그 사실을 명시한다.
4. 비정상적으로 좋은 결과(cycle 30% 이상 급감 등)는 먼저 의심한다: 로그에 추론이 실제로 실행됐는지(`inferences`), 레이어 cycle 합이 맞는지, 골든이 정말 비교됐는지(`warnings`에 "no golden"이 없는지).
5. HW 변경(티어 밖 파라미터)은 제안만 하고 사람의 승인 후 적용한다. 면적은 `area` 필드로 항상 함께 보고한다.
6. 빌드·실행·검증은 항상 하네스로 한다. 수동 make로 만든 결과는 ledger에 없으므로 존재하지 않는 것으로 취급한다.

## 도구 (전부 리포지토리 루트에서)
```
# 골든 재생성 (알고리즘/가중치 변경 시에만)
python3 tools/harness/make_golden.py [--gen DIR]

# 실험 1회: 빌드→실행→검증→ledger
python3 tools/harness/run_experiment.py --name NAME --tier small|medium|large --backend host|target \
    [--knob MACRO=VALUE ...] [--clips WAV ...] [--mac]
#   결과: experiments/NAME/result.json, experiments/NAME/<clip>/run.log|callgrind.out|trace.fst

# 골든 검증만
python3 tools/harness/verify_golden.py experiments/NAME/<clip>/run.log data/golden/<clip>.log

# 프로파일 질의 (callgrind)
python3 tools/callgrind/cg_query.py summary  CG              # 총량, CPI, 스톨 분해, 영역 접근 비율, top5
python3 tools/callgrind/cg_query.py top      CG --event Cycle [-n 15] [--inclusive]
python3 tools/callgrind/cg_query.py stalls   CG              # 함수별 Cycle/Ir/스톨 이벤트
python3 tools/callgrind/cg_query.py regions  CG              # 함수별 접근 영역 비율 (Acc* 이벤트)
python3 tools/callgrind/cg_query.py annotate CG --function F --source-root firmware
python3 tools/callgrind/cg_query.py callers  CG --function F
python3 tools/callgrind/cg_query.py diff     CG_A CG_B --event Cycle
#   --json 으로 기계 판독 출력

# 파형 (폐쇄망 4단계에서 작성 예정)
python3 tools/fst/bus_windows.py trace.fst --window-us 10 > windows.csv
python3 tools/fst/wave_at_pc.py trace.fst --pc 0x... --cycles 200

# 타깃 빌드만
cd firmware && make -f target.mk [LDSCRIPT=link/small.ld] [EXTRA_CFLAGS="-DOFFLOAD_PW1=1"]
cd firmware && make -f target.mk SMOKE=1            # 주변장치 스모크 테스트
```

## knob 목록 (firmware/app/placement.h)
- 배치: `SECTION_WEIGHTS_{CONV1,DW1..4,PW1..4,FC}`, `SECTION_ACT_A/B`, `SECTION_NN_SCRATCH`, `SECTION_FEATURES`, `SECTION_MFCC_WORK`, `SECTION_MFCC_TABLES`, `SECTION_AUDIO_RING`, `SECTION_CTRL`, `SECTION_CODE_{MFCC,NN,ISR}` — 값은 `PLACE_DATA(REGION_x)` / `PLACE_CODE(REGION_x)`, x ∈ FLASH ITCM DTCM SRAM0..3
  - 제약: 오디오 링과 DMA 복사 대상, MAC 입력은 TCM 불가. 코드는 ITCM/FLASH만.
- 가중치 로딩: `KWS_WEIGHT_LOAD_MODE` 0/1/2, `SECTION_WEIGHT_COPY`
- 오프로드: `OFFLOAD_{CONV1,PW1..4,FC}` 0/1, `OFFLOAD_WAIT_IRQ` 0/1 (MAC 있는 티어만 효과)
- DMA/버퍼: `AUDIO_DMA_BURST` 1/4/8/16, `AUDIO_RING_FRAMES` 2/3
- 컴파일러: `run_experiment.py`에는 없음. 필요하면 `EXTRA_CFLAGS`를 knob처럼 취급하도록 하네스에 `--cflag` 추가 (작은 수정)
- 스펙(건드리지 않음): `KWS_INFER_HOP_FRAMES`

셸에서 knob 값에 괄호가 있으면 따옴표: `--knob "SECTION_ACT_A=PLACE_DATA(REGION_DTCM)"`

## 포팅 단계 프로토콜 (HANDOFF.md 1~3단계)
1. 레지스터 맵을 받으면 `platform_regs.h`부터 고친다. 매크로 이름은 유지.
2. `hal_target.c`는 의미가 다른 곳만 고친다. 고치는 이유를 주석에 남긴다.
3. 스모크 테스트를 통과시키고 나서 펌웨어를 돌린다. 스모크 실패 항목을 두 개 이상 동시에 디버깅하지 않는다.
4. 펌웨어 골든 불일치 진단 순서: MFCC → cksum 첫 불일치 레이어 → CTRL. `HANDOFF.md` 3단계 참고.
5. 진행 상황은 `HANDOFF.md` 체크박스에 반영하고 커밋한다.

## 최적화 루프 프로토콜 (Stage 1 이후)
목표·제약은 사용자가 준다 (예: "Small 티어, avg_infer_cycles 최소화, 골든 통과, ISR ≤ 20 µs, overruns 0").

반복 1회:
1. **분석**: 최신 실험의 `cg_query summary/stalls/regions`로 지배적 비용과 원인을 한 문장으로 요약. 근거 숫자를 인용.
2. **가설**: knob 변경 1~2개와 기대 효과(방향과 대략 크기). "왜"를 적는다.
3. **실행**: `run_experiment.py`. 이름은 `s1_NNN_<짧은설명>`.
4. **판정**: `verify_ok` 확인 → `diff`로 함수별 변화 확인 → 가설과 비교. 예상과 다르면 원인을 찾고 나서 다음으로.
5. **기록**: ledger는 자동. 반복 5회마다 사람에게 표(실험명, knob diff, avg_infer_cycles, ISR µs, area, verify)와 다음 방향을 보고하고 승인을 받는다.

금지: 여러 knob를 한꺼번에 바꿔 무엇이 효과였는지 모르게 하는 것(탐색 목적으로 명시적으로 하는 경우 제외), 골든 실패 실험을 표에서 빼고 보고하는 것, HW 티어 밖 파라미터를 임의로 바꾸는 것.

## 결과 보고 형식
| 실험 | 티어 | knob 변경 | avg_infer_cycles | Δ% | ISR max µs | overruns | area | verify |
Pareto 요약(Stage 4): 티어별 최선 점 3개 + 베이스라인, 사람 손 최적화 점이 있으면 함께.

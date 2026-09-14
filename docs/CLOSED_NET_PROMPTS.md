# 폐쇄망 Claude Code 시작 프롬프트

리포지토리 루트에서 Claude Code를 열고 아래 블록을 첫 메시지로 붙여넣는다. `<>` 부분만 채운다.
비어 있는 환경 항목은 지우고 "0단계에서 직접 찾아서 보고하라"로 바꿔도 된다.

---

## 1. 첫 세션 (HANDOFF.md 0단계 + 1단계)

```
너는 이 리포지토리(co_opt_example)에서 HW-SW co-optimization PoC의 폐쇄망 단계를 진행한다.
먼저 CLAUDE.md 와 HANDOFF.md 를 읽고, 그 다음 docs/HAL_SPEC.md, docs/MEMORY_MAP.md,
firmware/hal/target/platform_regs.h, tools/harness/sim_run.py 를 읽어라.
개방망에서 완료된 것(HANDOFF.md "완료된 것" 표)은 다시 검증하지 말고 그대로 신뢰한다.

## 환경
- 플랫폼: GVSoC 기반을 대폭 수정한 자체 시뮬레이터. 코어는 Arm Cortex-M4 모델(CoreMark 정합성 97%+), 속도 0.5~10 MIPS
- 시뮬레이터 실행: <명령 예: sim --config <ir.json> --elf <elf> ...>
- 플랫폼 IR 스키마: <경로>, 기존 SoC 구성 예제: <경로>
- 모델 소스 트리: <경로> (신규 모델 추가 규약은 <예제 모델 경로> 참고)
- 출력: callgrind → <경로/방법>, FST 파형 → <경로/방법>, 로그 → <UART 모델 / 세미호스팅>
- 툴체인: arm-none-eabi-gcc <버전>, python3 <버전> (pip 미러 <있음/없음 → tools/wheels 사용>)
- 데이터: data/model/ds_cnn_s_quantized.tflite <있음/없음>, data/audio/<label>/*.wav <있음/없음>
- 버스 파형의 마스터 ID 신호: <있음(신호명)/없음/모름>

## 제약
- 토큰 예산이 제한적이다. 파일을 통째로 읽기보다 grep/특정 범위 읽기를 우선하고, 긴 로그·파형은 tools/ 의 질의 도구로 요약해서 본다. 같은 파일을 반복해서 읽지 않는다.
- 소스 수정 범위: platform_regs.h, hal_target.c(의미 차이 있는 곳만), sim_run.run_target(), tools/fst/signals.json, tools/tiers/*.json 의 platform_ir, 그리고 플랫폼 모델 소스. hal.h, app/, kernels/, third_party/, 로그 포맷은 수정 금지(CLAUDE.md 절대 규칙).
- 각 단계 완료 시 HANDOFF.md 체크박스를 갱신하고 커밋한다. 커밋 메시지는 단계 번호로 시작 ("1: platform_regs 교체").

## 지금 할 일 = HANDOFF.md 0단계 + 1단계
1. 기존 예제 SoC를 그대로 빌드·실행해 실행 명령, 출력 경로, 종료 조건을 확인한다.
2. docs/HAL_SPEC.md + platform_regs.h(제안 맵) 대비 실제 플랫폼의 차이를 표로 정리한다:
   레지스터 맵, IRQ 번호, DWT CYCCNT 지원, 로그 방식, 종료 방법, WAV 입력 방법, DMA 기능(burst/circular/src-fix),
   callgrind 이벤트 목록, 파형 신호명(core PC/func, bus master id/addr).
3. 표를 나에게 보여주고 승인을 받은 뒤 1단계를 실행한다:
   - tools/tiers/small.json 토폴로지를 IR로 작성 (가능하면 tools/harness/gen_platform_ir.py 로 자동화)
   - platform_regs.h 를 실제 값으로 교체, hal_target.c 는 필요한 곳만 수정
   - sim_run.run_target() 구현
   - cd firmware && make -f target.mk SMOKE=1 → 시뮬레이터에서 부팅, "SMOKE start" 로그 확인
4. 스모크 테스트 결과(PASS/FAIL 목록)와 함께 2단계(HW 모델·계측)에서 필요한 작업 목록을 보고하고 멈춘다.

보고는 표 위주로 짧게. 추측으로 레지스터 값을 채우지 말고 IR/모델 소스에서 확인한 값만 쓴다.
확인이 안 되는 것은 "미확인"으로 표시하고 질문해라.
```

3번의 승인 게이트를 둔 이유: 레지스터 맵을 잘못 해석한 채 HAL을 고치면 토큰이 크게 낭비된다. 표 검토에 1~2분만 쓰면 된다.

---

## 2. 이후 세션 (2~4단계)

```
CLAUDE.md 와 HANDOFF.md 를 읽고 HANDOFF.md 의 <N>단계를 진행해라.
이전 세션 결과는 HANDOFF.md 체크박스와 git log 에 있다. 완료된 항목은 다시 검증하지 않는다.
토큰 예산이 제한적이니 파일은 필요한 범위만 읽고, 로그·파형은 tools/ 의 질의 도구로 요약한다.
단계의 완료 기준을 만족하면 결과를 표로 보고하고, HANDOFF.md 를 갱신·커밋한 뒤 멈춘다.
```

2단계에서 MAC 가속기는 뒤로 미뤄도 된다 (Stage 4에서만 필요). 우선순위: Audio FIFO → 센서/결과/종료 레지스터 → callgrind 영역 이벤트 → DMA burst → MAC.

---

## 3. Stage 1 최적화 루프 시작 (HANDOFF.md 5단계)

```
CLAUDE.md 의 "최적화 루프 프로토콜"에 따라 Stage 1 을 시작한다.
목표: Small 티어에서 avg_infer_cycles 최소화.
제약: 골든 통과(verify_ok), ISR max ≤ 20 µs, overruns 0, 티어 밖 HW 파라미터 변경 금지.
베이스라인: experiments/ledger.jsonl 의 base_small (없으면 먼저 실행).
클립: data/audio_synth/two_tone.wav (회귀용 1개). 실험 이름은 s1_NNN_<설명>.
knob 변경은 1회에 1~2개, 변경 전에 cg_query summary/stalls/regions 근거를 한 문장으로 적는다.
반복 5회마다 표(실험, knob diff, avg_infer_cycles, Δ%, ISR µs, overruns, verify)와 다음 방향을 보고하고 멈춰라.
```

Stage 2 이후는 목표·제약 줄만 바꾼다 (예: "Medium 티어, 면적 중립 HW knob 포함", "Large 티어, OFFLOAD_* 포함").

---

## 4. 세션 운영 팁

- 리포지토리 자체가 세션 간 메모리다. 세션이 끝나기 전에 반드시 HANDOFF.md 갱신 + 커밋을 시키면 다음 세션 시작 비용이 최소가 된다.
- 문제를 두 개 이상 동시에 디버깅하게 두지 않는다. "X만 고치고 멈춰라"가 토큰 효율이 가장 좋다.
- 비정상적으로 좋은 결과가 나오면 CLAUDE.md 절대 규칙 4에 따라 스스로 의심하도록 되어 있지만, 사람이 한 번 더 본다.

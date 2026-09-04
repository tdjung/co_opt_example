# co_opt_example

AI 기반 HW-SW co-optimization PoC — Cortex-M4 가상 플랫폼, KWS(DS-CNN Small, CMSIS-NN) + 1 kHz 제어 루프.

| 문서 | 용도 |
|---|---|
| `docs/cm4_hw_sw_coopt_poc_plan.md` | 전체 계획·설계 결정 |
| `HANDOFF.md` | 폐쇄망에서 남은 작업 체크리스트 (여기서 시작) |
| `CLAUDE.md` | Claude Code 작업 지침 (포팅 단계 / 최적화 루프) |
| `docs/HAL_SPEC.md`, `docs/MAC_ACCEL_SPEC.md`, `docs/MEMORY_MAP.md` | 플랫폼 모델이 만족해야 할 동작 |

빠른 확인 (호스트):
```
python3 model/gen_mfcc_tables.py && python3 model/gen_weights.py
cd host && make -j8 && ./build/kws_host --wav ../data/audio_synth/two_tone.wav | grep -E "^\[infer|GOLDEN|CTRL|RUN"
cd .. && python3 tools/harness/run_experiment.py --name demo --tier small --backend host --clips data/audio_synth/chirp.wav
```
타깃: `cd firmware && make -f target.mk` (arm-none-eabi-gcc).

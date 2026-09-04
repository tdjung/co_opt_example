# HAL 스펙 — 플랫폼 모델이 만족해야 할 동작

`firmware/hal/hal.h`가 인터페이스, `hal/host/hal_host.c`가 동작 레퍼런스, `hal/target/hal_target.c`가
제안 레지스터 맵(`platform_regs.h`) 기준 구현이다. 폐쇄망에서는 **`platform_regs.h`를 실제 IR에 맞추고,
의미가 다른 부분만 `hal_target.c`를 고친다**. `hal.h`와 `app/`, `kernels/`는 건드리지 않는다.

## 시간·사이클
- `hal_cycles()`: DWT CYCCNT(코어 모델이 지원해야 함) 또는 플랫폼 CYCLE_COUNTER 레지스터.
- `hal_time_us()`: 시뮬레이션 시간(µs). 제어 루프의 setpoint 스텝(300 ms)에 사용.

## 오디오 소스 + DMA
- Audio source 모델: WAV(16 kHz/mono/int16)를 읽어 1 샘플/62.5 µs 속도로 FIFO에 공급. FIFO는 32bit 레지스터 읽기당 int16 1개. 파일 끝에서 STATUS.EOF=1, 이후 읽기는 0.
- DMA 채널 0: FIFO(고정 주소) → 링버퍼 슬롯(320 샘플 = 640 B). 완료 시 DONE + IRQ. HAL이 다음 슬롯으로 재시작. 플랫폼이 circular + half/full 인터럽트를 지원하면 그 방식이 더 현실적이니 교체 가능.
- burst: `DMA_CTRL_BURST(n)` beat 수. 버스 점유 패턴(따라서 경합)이 달라져야 knob으로 의미가 있다.
- DMA는 TCM에 접근할 수 없어야 한다(버스 오류 또는 무시). 링버퍼·가중치 복사 대상은 SRAM 뱅크.

## 타이머
- 다운카운터, LOAD 주기(cycle), 만료 시 STATUS.bit0 + IRQ1, 자동 재장전.
- ISR 지연 측정: `LOAD - VALUE`를 ISR 진입 시 읽음. 이 값 ≤ 20 µs(2000 cycle)가 제어 루프 제약.
- 우선순위: Timer(0) > DMA(1) > MAC(2). NVIC 우선순위를 코어 모델이 지원해야 한다.

## 센서/액추에이터 (플랜트 모델)
정수 1차 플랜트. 1 ms마다(시뮬레이션 시간 기준) 다음을 수행한 뒤 타이머 IRQ가 뜬다:
```
y = y + (((u - y) * 13) >> 8)      // 산술 시프트, int32
```
- SENSOR_VALUE 읽기 = y, ACTUATOR_VALUE 쓰기 = u. 리셋 값 0.
- 호스트 HAL과 동일한 순서(플랜트 갱신 → ISR)여야 `CTRL seq_cksum`이 일치한다.

## 결과/트레이스/종료
- RESULT_*: 추론 결과 기록(파형에서 보이도록). 필수는 아님.
- TRACE_MARKER: 쓰기 값이 파형 신호로 나오면 `wave_at_pc` 대신 마커로 구간을 찾을 수 있다. 마커 ID: 0x10 MFCC 시작, 0x20/0x21 추론 시작/끝, 0x100+i 레이어 i.
- SIM_EXIT: 쓰기 시 시뮬레이션 종료(종료 코드 = 값). callgrind/FST가 이 시점에 flush 되어야 한다.
- LOG_TX: 바이트 쓰기 → run.log. 세미호스팅으로 대체 가능(`hal_log_puts` 수정).

## MAC 가속기
`docs/MAC_ACCEL_SPEC.md`.

## 메모리 맵
`docs/MEMORY_MAP.md`. 링커 스크립트는 `tools/harness/gen_linker.py`가 티어 JSON에서 생성한다.

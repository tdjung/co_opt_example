폐쇄망용 Python 패키지 (pip 미러가 없을 때):
```
pip install tools/wheels/*.whl
```
- pylibfst: FST 읽기 (`tools/fst/fst_tools.py`). cp312 x86_64 wheel (다른 Python 버전이면 `pip download pylibfst`로 교체).
- tflite: tflite 플랫버퍼 스키마 리더 (`model/gen_weights.py --tflite`). flatbuffers 필요.
numpy, flatbuffers, cffi는 일반적으로 설치되어 있다고 가정. 없으면 같은 방식으로 추가.

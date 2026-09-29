# 발표 자료 (System Expert 현업과제)

| 파일 | 용도 |
|---|---|
| `SCRIPT.md` | 발표 대본 (장별 누적 시간, Q&A 답변) |
| `deck_preview.html` | 오프라인 뷰어 — 브라우저로 열기. ←/→ 이동, N 발표자 노트, F 전체화면 |
| `slides/*.html` | 장별 소스 (1920×1080, 상단 116px 검정 띠 = 템플릿 소제목 자리) |
| `deck.json` | 장 순서 |

템플릿 PPT로 옮길 때: 검정 띠 문구 → 템플릿 소제목, 본문 첫 헤드라인 → 본문 맨 위, 그림·표는 `deck_preview.html`을 보며 재구성.
`deck_preview.html`은 검토용 근사 렌더링이라 아이콘·화살표 도형은 단순화되어 보인다.

채울 자리: 표지 `[발표자/조직]`, 6장 스크린샷·영상, 8장 아키텍처 그림, 9장 실측치, 10장 정량·교육 항목.

## PPTX
| 파일 | 폰트 |
|---|---|
| `deck_current_fonts.pptx` | IBM Plex Sans / JetBrains Mono (설치되지 않은 PC에서는 대체 폰트로 표시) |
| `deck_basic_fonts.pptx` | Arial / Courier New (어디서나 동일하게 표시) |

둘 다 `build_pptx.js`로 생성 (`node build_pptx.js current|basic`, pptxgenjs 필요). 내용·순서·발표자 노트는 슬라이드 HTML과 동일.
템플릿 PPT로 옮길 때는 이 pptx에서 도형·텍스트를 복사해 붙여 넣는 편이 HTML보다 빠르다.

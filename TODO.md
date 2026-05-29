# TODO — Jasp Keyboard Joystick (Hall stick build)

## 부품/계획
- 스틱: K-Silver PS5 DualSense 호환 **홀이펙트** 스틱 (공급전압 1.7~5.5V → **5V 구동 OK**)
- TMR 스틱은 DualSense용으로 보관 (둘 다 DualSense 호환 = 교체 가능)
- 보드: USB-C Pro Micro (Arduino Leonardo로 플래시)

## 단계
- [ ] **1. 펌웨어 플래시** — `sketch_promicro.ino`, 보드 = Arduino Leonardo, MHeironimus Joystick 라이브러리(ZIP) 설치
- [ ] **2. 브레드보드 임시 테스트** (납땜 전 동작 확인)
  - **접속 방식 결정됨: Pro Micro에 전체 핀 헤더 납땜 → 브레드보드 정상 사용**
  - 스틱 쪽은 핀이 없으므로 패드에 짧은 점퍼 와이어 납땜 → 브레드보드에 꽂기
  - 필요한 연결 5개: `VCC(5V)`, `GND`, `A0`, `A1`, `14`
- [ ] **3. 시리얼 설정** — 115200 baud, `cal`로 중심 잡기, 필요시 `dz`/`sens`/`invx`/`invy`
- [ ] **4. 최종 직접 납땜** — 28AWG로 스틱 패드 ↔ Pro Micro 직결 (배선도 기준)

## ⚠️ 미결: 축 핀 불일치
- 배선도: X→A0(노랑), Y→A1(초록)
- 펌웨어: `X_PIN=A1`, `Y_PIN=A0` (반대)
- → 축이 바뀌어 잡히면: 펌웨어 핀 정의 스왑 **또는** A0/A1 배선 스왑 **또는** 시리얼 `invx/invy`
- 결정 보류 중

## 배선 (배선도 기준)
| Pro Micro | 스틱 | 비고 |
|-----------|------|------|
| VCC(5V) 🔴 | X-VCC + Y-VCC | 양쪽 분기 |
| GND 🔵 | X-GND + Y-GND + SW-GND | 셋 분기 |
| A0 🟡 | X signal | |
| A1 🟢 | Y signal | |
| 14 🟣 | Switch | 내부 풀업, active-low |

## 참고
- 배선도: `jasp-keyboard-joystick/ProMicro_Wiring_Diagram.png`
- 빌드 영상: https://www.youtube.com/watch?v=S8SKIpWGIe8

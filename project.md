# 4가지 모드 지원 시계

## 전체 Task 동기화 관련

### 매크로 상수 정의

- CLOCK_DISPLAY
- CLOCK_HH_EDIT
- CLOCK_MM_EDIT

### 변수 정의

- Mode : 전체 동작 모드 관리 전역변수
- Sem : 크리티컬 보호용 세마포어
- SwitchFlag : ControlTask 동기화 이벤트 플래그. 가장 높은 우선순위의 ControlTask를 스위치 눌림 이벤트까지 블록시킴. 나머지 테스크에 실행을 양도.
- TaskControlFlag : ControlTask가 나머지 테스크를 동기화 하기위한 이벤트 플래그
- Sw1 : 스위치1 눌림 체크 전역 변수
- Sw2 : 스위치2 눌림 체크 전역 변수

### Task 정의

- ControlTask : 전체 Task 실행 순서 관리 테스크

## 24시간 디스플레이 시계

### 변수 정의

- ClockSCount : 시간을 관리하기 위해서 도입한, 86,400 초 (24 시간)를 카운트 할 수 있는 전역 변수.

### Task 정의

- 

### Function 정의

- void display_fnd(int count)

## coding convention

- Task 함수 정의 : 파스칼 케이스

## os_cfg.h 수정사항

### 2019-11-27

- event flag 관련 설정 변경
  - OS_FLAG_EN : 1로 변경. event flag 사용을 위해서
  - OS_FLAG_ACCEPT_EN : 1로 변경. 스위치가 눌렸는지 체크할때 non-block 기능 사용을 위해서

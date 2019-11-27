# 4가지 모드 지원 시계

## 전체 Task 동기화 관련

### 매크로 상수 정의

- CLOCK_DISPLAY
- CLOCK_HH_EDIT
- CLOCK_MM_EDIT

### 변수 정의

- Mode : 전체 동작 모드 관리 전역변수
- Sem : 크리티컬 보호용 세마포어

### Task 정의

- ControlTask : 전체 Task 실행 순서 관리 테스크

## 24시간 디스플레이 시계

### 변수 정의

- ClockSCount : 시간을 관리하기 위해서 도입한, 86,400 초 (24 시간)를 카운트 할 수 있는 전역 변수.

### Task 정의

- 

## coding convention

- Task 함수 정의 : 파스칼 케이스

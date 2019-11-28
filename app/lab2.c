#include "includes.h"

#define F_CPU	16000000UL	// CPU frequency = 16 Mhz
#include <avr/io.h>
#include <avr/interrupt.h>	// interrupt 관련
#include <util/delay.h>

#define  TASK_STK_SIZE  OS_TASK_DEF_STK_SIZE
#define  N_TASKS        3	// *

/* timer1 1024 prescaling의 경우 초 단위 clock 개수*/
#define HALF -7812
#define ONE -15625

/* 시계 깜빡임 효과를 위한 상수 */
#define BLANK	-1

#define CLOCK_DISPLAY 	0
#define CLOCK_HH_EDIT 	1
#define CLOCK_MM_EDIT 	2

#define TIMER_DISPLAY 	3
#define TIMER_HH_EDIT 	4
#define TIMER_MM_EDIT 	5
#define TIMER_ALARM		6

#define TEMP_DISPLAY	7
#define LIGHT_DISPLAY 	8

typedef unsigned char uc;
const uc digit[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x27, 0x7f, 0x6f};
const uc fnd_sel[4] = {0x01, 0x02, 0x04, 0x08};
const uc dot = 0x80;

OS_STK        TaskStk[N_TASKS][TASK_STK_SIZE];
OS_EVENT	  *Sem;			    // 크리티컬 섹션 보호용 세마포어
OS_FLAG_GRP   *SwitchFlag;		// ControlTask 동기화 이벤트 플래그
OS_FLAG_GRP   *TaskControlFlag;	// ControlTask가 나머지 테스크를 동기화 하기위한 이벤트 플래그

volatile INT8U  	Mode;		// 전체 동작 모드 관리 전역변수
volatile BOOLEAN 	Sw1;		// 스위치1 눌림 체크 전역 변수
volatile BOOLEAN 	Sw2;		// 스위치2 눌림 체크 전역 변수
volatile INT8U 		err;

volatile INT32U  ClockSCount;	// 시간 카운트 변수

void  ControlTask(void *data);  // 전체 테스크 실행 순서 관리 테스크
void  ClockTask(void *data);	// 시계 모드 관리 테스크
void  ClockDisplayTask(void *data);	// FND에 시간을 출력하는 테스크

void display_clock(int hh, int mm, int d);
void change_mode(void);
void switch_task(void);


/* 인터럽트 핸들러 정의 */
// Sw1
ISR(INT4_vect) {
	Sw1 = TRUE;
	OSFlagPost(SwitchFlag, 0x01, OS_FLAG_SET, &err);
	_delay_ms(100);  // debouncing
}

// Sw2
ISR(INT5_vect) {
	Sw2 = TRUE;
	OSFlagPost(SwitchFlag, 0x02, OS_FLAG_SET, &err);
	_delay_ms(100);  // debouncing
}

// Timer1. 1초씩 증가
ISR(TIMER1_OVF_vect) {
	ClockSCount = (ClockSCount + 1) % 86400;
	TCNT1 = ONE;
}

int main (void)
{
	OSInit();

	// OSTimeDly 쓰기 위해 타이머 0를 적절히 설정
	OS_ENTER_CRITICAL();
	TCCR0 = 0x07;
	TIMSK = _BV(TOIE0);
	TCNT0 = 256 - (CPU_CLOCK_HZ / OS_TICKS_PER_SEC / 1024);
	OS_EXIT_CRITICAL();

	// 초기 셋팅
	ClockSCount = 86340;
	Mode = CLOCK_DISPLAY;

	OS_ENTER_CRITICAL();
	TCCR1B = ((1 << CS12) | (0 << CS11) | (1 << CS10)); // timer1 1024 prescaling
	TIMSK |= (1 << TOIE1);	// timer1 overflow interrupt enabled
	TCNT1 = ONE;
	OS_EXIT_CRITICAL();
	
	// fnd 설정
	DDRC = 0xff;
    DDRG = 0x0f;

	// 디버거용 led
	DDRA = 0xff;

    // 스위치 설정
    DDRE = 0xcf;        // 0b1100 1111
    EICRB = 0x0a;       // 0b0000 1010, falling edge triger
    EIMSK = 0x30;       // 0b0011 0000,

	Sem = OSSemCreate(1);
	SwitchFlag = OSFlagCreate(0x00, &err);
	TaskControlFlag = OSFlagCreate(0x03, &err);		// CLOCK_DISPLAY 모드로 시작

	OSTaskCreate(ControlTask, (void *)0, (void *)&TaskStk[0][TASK_STK_SIZE - 1], 0);
	OSTaskCreate(ClockTask, (void *)0, (void *)&TaskStk[1][TASK_STK_SIZE - 1], 1);
	OSTaskCreate(ClockDisplayTask, (void *)0, (void *)&TaskStk[2][TASK_STK_SIZE - 1], 2);

    sei();		// 전체 인터럽트 허용
	OSStart();

	return 0;
}

/************************************ Task 정의 ****************************************/

void ControlTask (void *data)
{
	INT8U err;

	for(;;) {

		// 스위치 눌림 체크
		// 여기서 blocking 되어 다른 Task에게 실행 양도
		OSFlagPend(SwitchFlag, 0x03, OS_FLAG_WAIT_SET_ANY + OS_FLAG_CONSUME, 0, &err);

		// 눌린 스위치에 따른 Mode 값 변경
		OSSemPend(Sem, 0, &err);
		change_mode();
		OSSemPost(Sem);

		// TaskControlFlag를 적절히 설정하여 현재 Mode 값에 알맞는 Task들 실행
		OSSemPend(Sem, 0, &err);
		switch_task();
		OSSemPost(Sem);
	}
}

void ClockTask (void *data)
{
	INT8U err;
	INT32U temp_hour;

	for (;;) {
		PORTA ^= 0x80;
		OSFlagPend(TaskControlFlag, 0x01, OS_FLAG_WAIT_SET_ALL, 0, &err);
		
		if (Sw2 == TRUE) {
			if (Mode == CLOCK_HH_EDIT) {
				ClockSCount = (ClockSCount + 3600) % 86400;
			}
			else if (Mode == CLOCK_MM_EDIT) {
				temp_hour = ClockSCount / 3600;
				ClockSCount = (ClockSCount + 60) % 86400;

				// 분 단위 변경으로 인해 시간 단위가 변경되는 것 방지
				if (temp_hour != ClockSCount / 3600) {
					ClockSCount = temp_hour * 3600;
				}
			}
			Sw2 = FALSE;
		}
		OSTimeDly(100);
	}
}

void ClockDisplayTask (void *data)
{
	INT8U err;
	INT32U temp_time;
	int hh, mm, d;
	int i;

	for (;;) {
		OSFlagPend(TaskControlFlag, 0x02, OS_FLAG_WAIT_SET_ALL, 0, &err);
		temp_time = ClockSCount;
		hh = temp_time / 3600;
		mm = (temp_time % 3600) / 60;
		d = TRUE;

		for (i = 0; i < 50; i++) {
			display_clock(hh, mm, d);
		}

		if (Mode == CLOCK_HH_EDIT) {
			hh = BLANK;
		}
		else if (Mode == CLOCK_MM_EDIT) {
			mm = BLANK;
		}
		d = BLANK;

		for (i = 0; i < 50; i++) {
			display_clock(hh, mm, d);
		}
	}
}

/************************************ Task 정의 끝 **************************************/

/************************************ 사용 함수 정의 ************************************/

/*
	FND 화면에 숫자를 출력하는 테스크
*/
void display_clock(int hh, int mm, int d) {
    int i;
	uc fnd[4];

	if (hh == BLANK) {
		fnd[3] = 0;
		fnd[2] = 0;
	}
	else {
		fnd[3] = digit[(hh / 10) % 10];
		fnd[2] = digit[hh % 10];
	}
    
	if (mm == BLANK) {
		fnd[1] = 0;
		fnd[0] = 0;
	}
	else {
		fnd[1] = digit[(mm / 10) % 10];
		fnd[0] = digit[mm % 10];		
	}

	if (d != BLANK) {
		fnd[2] += dot;
	}

    for (i = 0; i < 4; i++) {
        PORTC = fnd[i];
        PORTG = fnd_sel[i];
        _delay_us(2500);
    }
}

void change_mode() {
	if (Sw1 == TRUE) {
		if (Mode == CLOCK_DISPLAY) {
			TIMSK &= ~(1 << TOIE1);	// timer1 overflow interrupt disabled
			Mode = CLOCK_HH_EDIT;
			Sw1 = FALSE;
		}
		else if (Mode == CLOCK_HH_EDIT) {
			Mode = CLOCK_MM_EDIT;
			Sw1 = FALSE;
		}
		else if (Mode == CLOCK_MM_EDIT) {
			TIMSK |= (1 << TOIE1);	// timer1 overflow interrupt enabled
			Mode = CLOCK_DISPLAY;
			Sw1 = FALSE;
		}
		return;
	}
	// if (Sw2 == TRUE) {
	// 	if (Mode == CLOCK_DISPLAY) {
	// 		Mode = TIMER_DISPLAY;
	// 		Sw2 = FALSE;
	// 	}
	// 	else if (Mode == TIMER_DISPLAY) {
	// 		Mode = TEMP_DISPLAY;
	// 		Sw2 = FALSE;
	// 	}
	// 	else if (Mode == TEMP_DISPLAY) {
	// 		Mode = LIGHT_DISPLAY;
	// 		Sw2 = FALSE;
	// 	}
	// 	else if (Mode == LIGHT_DISPLAY) {
	// 		Mode = CLOCK_DISPLAY;
	// 		Sw2 = FALSE;
	// 	}
	// 	// *
	// }
}

/*
	현재 Mode에 알맞는 TaskControlFlag 값을 설정해준다.
*/
void switch_task() {
	INT8U err;

	if (Mode == CLOCK_DISPLAY || Mode == CLOCK_HH_EDIT || Mode == CLOCK_MM_EDIT) {
		OSFlagPost(TaskControlFlag, 0x03, OS_FLAG_SET, &err);
	}
	else if (Mode == TIMER_DISPLAY || Mode == TIMER_HH_EDIT || 
			 Mode == TIMER_MM_EDIT || Mode == TIMER_ALARM) {
		
	}
	else if (Mode == TEMP_DISPLAY) {

	}
	else if (Mode == LIGHT_DISPLAY) {

	}
}

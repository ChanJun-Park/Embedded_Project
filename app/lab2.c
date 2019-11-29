#include "includes.h"

#define F_CPU	16000000UL	// CPU frequency = 16 Mhz
#include <avr/io.h>
#include <avr/interrupt.h>	// interrupt 관련
#include <util/delay.h>

#define  TASK_STK_SIZE  OS_TASK_DEF_STK_SIZE
#define  N_TASKS        4	// *

/* timer1 1024 prescaling의 경우 초 단위 clock 개수*/
#define ONE_SEC -15625

/* Buzzer on/off */
#define ON 1
#define OFF 0

/* timer2 32 prescaling 음계 */
#define DO 17
#define RE 43
#define MI 66
#define FA 77
#define SOL 97
#define LA 114
#define TI 129
#define UDO 137

/* 멜로디 박자 관련 상수 */
#define QUVR 125
#define QUTR 250
#define HALF 500
#define ONE 1000
#define TWO 2000
#define FOUR 4000

#define CLOCK_DISPLAY 	0
#define CLOCK_HH_EDIT 	1
#define CLOCK_MM_EDIT 	2

#define TIMER_STOP 		3
#define TIMER_MM_EDIT 	4
#define TIMER_SS_EDIT 	5
#define TIMER_COUNT		6
#define TIMER_ALARM		7

#define TEMP_DISPLAY	8
#define LIGHT_DISPLAY 	9

typedef unsigned char uc;
const uc digit[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x27, 0x7f, 0x6f};
const uc fnd_sel[4] = {0x01, 0x02, 0x04, 0x08};
const uc dot = 0x80;

volatile INT8U state = OFF;
volatile INT8U mel_idx = 0;
volatile INT8U note_idx = 0;

/* my face like apple */
const uc melody[] = {DO, RE, MI, MI, FA, MI, RE, 
                     RE, MI, FA, FA, SOL, FA, MI, 
					 MI, FA, SOL, SOL, UDO, LA, SOL, SOL, 
					 DO, RE, MI, MI, RE, DO};

const INT16U note[] = {HALF, HALF, ONE, ONE, HALF, HALF, TWO,
					HALF, HALF, ONE, ONE, HALF, HALF, TWO,
					HALF, HALF, ONE, ONE, HALF, HALF, ONE, ONE,
					HALF, HALF, ONE, ONE, ONE, TWO};

OS_STK        TaskStk[N_TASKS][TASK_STK_SIZE];
OS_EVENT	  *Sem;			    // 크리티컬 섹션 보호용 세마포어
OS_EVENT 	  *TimerSem;		// 타이머 테스크 동기화용 세마포어
OS_FLAG_GRP   *SwitchFlag;		// ControlTask 동기화 이벤트 플래그
OS_FLAG_GRP   *TaskControlFlag;	// ControlTask가 나머지 테스크를 동기화 하기위한 이벤트 플래그

volatile INT8U  	Mode;		// 전체 동작 모드 관리 전역변수
volatile BOOLEAN 	Sw1;		// 스위치1 눌림 체크 전역 변수
volatile BOOLEAN 	Sw2;		// 스위치2 눌림 체크 전역 변수
volatile INT8U 		err;

volatile INT32U  ClockSCount;	// 시간 카운트 변수
volatile uc 	 ClockFnd[4];	// 현재 시간 (HH:MM)

volatile INT16U  TimerSCount;	// 타이머 카운트 변수
volatile uc 	 TimerFnd[4];	// 현재 타이머 시간 (MM:SS)
volatile BOOLEAN TimesUp;

void ControlTask(void *data);  	// 전체 테스크 실행 순서 관리 테스크
void ClockTask(void *data);	   	// 시계 모드 관리 테스크
void TimerAlarmTask(void *data);// 타이머 알람 출력 테스크
void TimerTask(void *data);    	// 타이머 모드 관리 테스크

void calculate_hh_mm(INT32U scount);
void calculate_mm_ss(INT16U scount);
void display_fnd(uc * fnd);
void change_mode(void);
void switch_task(void);


/* 인터럽트 핸들러 정의 */
// Sw1
ISR(INT4_vect) {
	Sw1 = TRUE;
	OSFlagPost(SwitchFlag, 0x01, OS_FLAG_SET, &err);
	_delay_ms(10);  // debouncing
}

// Sw2
ISR(INT5_vect) {
	INT8U i;
	Sw2 = TRUE;
	if (Mode == CLOCK_HH_EDIT) {
		ClockSCount = (ClockSCount + 3600) % 86400;
		calculate_hh_mm(ClockSCount);
		Sw2 = FALSE;
	}
	else if (Mode == CLOCK_MM_EDIT) {
		INT32U temp_hour = ClockSCount / 3600; 
		ClockSCount = (ClockSCount + 60) % 86400;

		// 분 단위 변경으로 인해 시간 단위가 변경되는 것 방지
		if (temp_hour != ClockSCount / 3600) {
			ClockSCount = temp_hour * 3600;
		}
		calculate_hh_mm(ClockSCount);
		Sw2 = FALSE;
	}
	OSFlagPost(SwitchFlag, 0x02, OS_FLAG_SET, &err);
	_delay_ms(10);  // debouncing
}

// Timer2 음계 출력
ISR (TIMER2_OVF_vect) {
	if (Mode == TIMER_ALARM) {
		if (state == ON) {
			PORTB = 0x00;
			state = OFF;
		}
		else {
			PORTB = 0x10;
			state = ON;
		}
	}
    else {
		state = OFF;
		PORTB = 0x00;
	}
    TCNT2 = melody[mel_idx];
}

// Timer1. 1초씩 증가
ISR(TIMER1_OVF_vect) {
	TCNT1 = ONE_SEC;
	ClockSCount = (ClockSCount + 1) % 86400;
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
	ClockSCount = 86340;	// 23시 59분
	Mode = CLOCK_DISPLAY;
	TimerSCount = 0;
	TimesUp = FALSE;

	// 시간을 계산하기 위해 타이머 1를 적절히 설정
	OS_ENTER_CRITICAL();
	TCCR1B = ((1 << CS12) | (0 << CS11) | (1 << CS10)); // timer1 1024 prescaling
	TIMSK |= (1 << TOIE1);	// timer1 overflow interrupt enabled
	TCNT1 = ONE;
	OS_EXIT_CRITICAL();
	
	// 버저를 이용하기 위해 타이머 2를 적절히 설정
	TCCR2 = ((0 << CS22) | (1 << CS21) | (1 << CS20));	// timer2 clock 32 prescaling

	// fnd 설정
	DDRC = 0xff;
    DDRG = 0x0f;

	// 디버그용 led
	DDRA = 0xff;

	// buzzer
	DDRB = 0x10;

    // 스위치 설정
    DDRE = 0xcf;        // 0b1100 1111
    EICRB = 0x0a;       // 0b0000 1010, falling edge triger
    EIMSK = 0x30;       // 0b0011 0000,

	Sem = OSSemCreate(1);
	TimerSem = OSSemCreate(1);
	SwitchFlag = OSFlagCreate(0x00, &err);
	TaskControlFlag = OSFlagCreate(0x01, &err);		// CLOCK_DISPLAY 모드로 시작

	OSTaskCreate(ControlTask, (void *)0, (void *)&TaskStk[0][TASK_STK_SIZE - 1], 0);
	OSTaskCreate(ClockTask, (void *)0, (void *)&TaskStk[1][TASK_STK_SIZE - 1], 1);
	OSTaskCreate(TimerAlarmTask, (void*)0, (void *)&TaskStk[2][TASK_STK_SIZE - 1], 2);
	OSTaskCreate(TimerTask, (void*)0, (void *)&TaskStk[3][TASK_STK_SIZE - 1], 3);

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
	INT8U i;
	INT32U temp_hour;

	for (;;) {
		OSFlagPend(TaskControlFlag, 0x01, OS_FLAG_WAIT_SET_ALL, 0, &err); // no consume
		calculate_hh_mm(ClockSCount);

		// 현재 시간 출력
		// 깜빡거리는 효과를 위해서 0.5초는 정상출력, 0.5초는 FND의 특정 세그먼트를 제외하고 출력
		ClockFnd[2] += dot;
		for (i = 0; i < 50; i++) {
			display_fnd(ClockFnd);
		}

		ClockFnd[2] -= dot;
		if (Mode == CLOCK_HH_EDIT) {
			ClockFnd[3] = 0;
			ClockFnd[2] = 0;
		}
		else if (Mode == CLOCK_MM_EDIT) {
			ClockFnd[1] = 0;
			ClockFnd[0] = 0;
		}
		
		for (i = 0; i < 50; i++) {
			display_fnd(ClockFnd);
		}
	}
}

void TimerAlarmTask(void *data)
{
	INT8U err;

	for(;;) {
		OSSemPend(TimerSem, 0, &err);
		TIMSK |= (1 << TOIE2);	// timer2 overflow interrupt enabled
		while(Mode == TIMER_ALARM) {
			TCNT2 = melody[mel_idx];

			while(1) {
				OSTimeDly(note[note_idx]);
				// _delay_ms(note[note_idx]);
				note_idx = (note_idx + 1) % 28;
				mel_idx = (mel_idx + 1) % 28;
			}
		}
		TIMSK &= ~(1 << TOIE2);	// timer2 overflow interrupt disabled
	}
}

void TimerTask (void * data)
{
	INT8U err;
	INT8U i;

	for(;;) {
		OSFlagPend(TaskControlFlag, 0x02, OS_FLAG_WAIT_SET_ALL, 0, &err); // no consume
		
		calculate_mm_ss(TimerSCount);
		if (Mode == TIMER_STOP) {
			if (TimesUp == TRUE) {
				TimesUp = FALSE;
				Mode = TIMER_ALARM;
				OSSemPost(TimerSem);
			}

			TimerFnd[2] += dot;
			for (i = 0; i < 100; i++) {
				display_fnd(TimerFnd);
			}
		}
		else if (Mode == TIMER_COUNT) {

			TimerFnd[2] += dot;
			for (i = 0; i < 50; i++) {
				display_fnd(TimerFnd);
			}
			TimerFnd[2] -= dot;
			for (i = 0; i < 50; i++) {
				display_fnd(TimerFnd);
			}

			TimerSCount -= 1;
			if (TimerSCount == 0) {
				Mode == TIMER_STOP;
				TimesUp = TRUE;
			}
		}
		else { // if (Mode == TIMER_MM_EDIT || Mode == TIMER_SS_EDIT)
			TimerFnd[2] += dot;
			for (i = 0; i < 50; i++) {
				display_fnd(TimerFnd);
			}
			TimerFnd[2] -= dot;
			if (Mode == TIMER_MM_EDIT) {
				TimerFnd[3] = 0;
				TimerFnd[2] = 0;
			}
			else if (Mode == TIMER_SS_EDIT) {
				TimerFnd[1] = 0;
				TimerFnd[0] = 0;
			}

			for (i = 0; i < 50; i++) {
				display_fnd(TimerFnd);
			}
		}
	}
}

/************************************ Task 정의 끝 **************************************/

/************************************ 사용 함수 정의 ************************************/

void calculate_hh_mm(INT32U scount) {
	INT8U hh, mm;

	hh = scount / 3600;
	mm = (scount % 3600) / 60;

	ClockFnd[3] = digit[hh / 10];
	ClockFnd[2] = digit[hh % 10];
	ClockFnd[1] = digit[mm / 10];
	ClockFnd[0] = digit[mm % 10];
}

void calculate_mm_ss(INT16U scount) {
	INT8U mm, ss;

	mm = scount / 60;
	ss = scount % 60;

	TimerFnd[3] = digit[mm / 10];
	TimerFnd[2] = digit[mm % 10];
	TimerFnd[1] = digit[ss / 10];
	TimerFnd[0] = digit[ss % 10];
}

// 주어진 배열에 알맞는 값을 FND에 출력하는 함수
// 4개의 세그먼트를 출력하는데 10ms가 소모된다.
void display_fnd(uc * fnd) {
	INT8U i;
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
		else if (Mode == TIMER_STOP) {
			Mode = TIMER_MM_EDIT;
			Sw1 = FALSE;
		}
		else if (Mode == TIMER_MM_EDIT) {
			Mode = TIMER_SS_EDIT;
			Sw1 = FALSE;
		}
		else if (Mode == TIMER_SS_EDIT) {
			Mode = TIMER_COUNT;
			Sw1 = FALSE;
		}
		else if (Mode == TIMER_ALARM) {
			Mode = TIMER_STOP;
			Sw1 = False;
		}
		return;
	}
	if (Sw2 == TRUE) {
		if (Mode == CLOCK_DISPLAY) {
			Mode = TIMER_STOP;
			Sw2 = FALSE;
		}
		else if (Mode == TIMER_STOP) {
			Mode = TEMP_DISPLAY;
			Sw2 = FALSE;
		}
		else if (Mode == TEMP_DISPLAY) {
			Mode = LIGHT_DISPLAY;
			Sw2 = FALSE;
		}
		else if (Mode == LIGHT_DISPLAY) {
			Mode = CLOCK_DISPLAY;
			Sw2 = FALSE;
		}
		// *
	}
}

/*
	현재 Mode에 알맞는 TaskControlFlag 값을 설정해준다.
*/
void switch_task() {
	INT8U err;

	if (Mode == CLOCK_DISPLAY || Mode == CLOCK_HH_EDIT || Mode == CLOCK_MM_EDIT) {
		OSFlagPost(TaskControlFlag, 0x01, OS_FLAG_SET, &err);
	}
	else if (Mode == TIMER_DISPLAY || Mode == TIMER_HH_EDIT || 
			 Mode == TIMER_MM_EDIT || Mode == TIMER_ALARM) {
		OSFlagPost(TaskControlFlag, 0x02, OS_FLAG_SET, &err);
	}
	else if (Mode == TEMP_DISPLAY) {

	}
	else if (Mode == LIGHT_DISPLAY) {

	}
}

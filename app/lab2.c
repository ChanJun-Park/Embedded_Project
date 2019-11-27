#include "includes.h"

#define F_CPU	16000000UL	// CPU frequency = 16 Mhz
#include <avr/io.h>
#include <util/delay.h>

#define  TASK_STK_SIZE  OS_TASK_DEF_STK_SIZE
#define  N_TASKS        3	// *

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

OS_STK       TaskStk[N_TASKS][TASK_STK_SIZE];
OS_EVENT	  *Sem;			    // 크리티컬 섹션 보호용 세마포어
OS_FLAG_GRP   *SwitchFlag;		// ControlTask 동기화 이벤트 플래그
OS_FLAG_GRP   *TaskControlFlag;	// ControlTask가 나머지 테스크를 동기화 하기위한 이벤트 플래그

volatile INT8U  Mode;		    // 전체 동작 모드 관리 전역변수
volatile BOOLEAN 	Sw1;		// 스위치1 눌림 체크 전역 변수
volatile BOOLEAN 	Sw2;		// 스위치2 눌림 체크 전역 변수

volatile INT8U  ClockSCount;	// 시간 카운트 변수

void  ControlTask(void *data);  // 전체 테스크 실행 순서 관리 테스크
void  ClockTask(void *data);	// 시계 모드 관리 테스크
void  ClockDisplayTask(void *data);	// FND에 시간을 출력하는 테스크

void display_clock(int count);
void switch_check();
void change_mode();


/* 인터럽트 핸들러 정의 */

int main (void)
{
	INT8U err;
	OSInit();
	// 여기 코드는 왜 있는 걸까?
	OS_ENTER_CRITICAL();
	TCCR0 = 0x07;
	TIMSK = _BV(TOIE0);
	TCNT0 = 256 - (CPU_CLOCK_HZ / OS_TICKS_PER_SEC / 1024);
	OS_EXIT_CRITICAL();

	// 초기 셋팅
	ClockSCount = 0;
	Mode = CLOCK_DISPLAY;

	Sem = OSSemCreaete(1);
	SwitchFlag = OSFlagCreate(0x00, &err);
	TaskControlFlag = OSFlagCreate(0x03, &err);		// CLOCK_DISPLAY 모드로 시작

	OSTaskCreate(ControlTask, (void *)0, (void *)&TaskStk[0][TASK_STK_SIZE - 1], 0);
	OSTaskCreate(ClockTask, (void *)0, (void *)&TaskStk[1][TASK_STK_SIZE - 1], 1);
	OSTaskCreate(ClockDisplayTask, (void *)0, (void *)&TaskStk[2][TASK_STK_SIZE - 1], 2);

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
		OSSemPend(Sem, &err);
		switch_check();
		OSSemPost(Sem);

		// TaskControlFlag를 적절히 설정하여 현재 Mode 값에 알맞는 Task들 실행
		OSSemPend(Sem, &err);
		change_mode();
		OSSemPost(Sem);
	}
}

void ClockTask (void *data)
{
	INT8U err;

	for (;;) {


		OSTimeDly(100);		// ClockDisplayTask를 실행하기 위한 양도
	}
}

void ClockDisplayTask (void *data)
{
	INT8U err;

	for (;;) {
		display_clock(ClockSCount);
	}

}

/************************************ Task 정의 끝 **************************************/

/************************************ 사용 함수 정의 ************************************/

/*
	FND 화면에 숫자를 출력하는 테스크
*/
void display_clock(int count) {
    int i;
	int hour;
	int minute;
	uc fnd[4];

	hour = count / 3600;
	minute = (count % 3600) / 60;

    fnd[3] = digit[(hour / 10) % 10];
    fnd[2] = digit[hour % 10] + dot;
    fnd[1] = digit[(minute / 10) % 10];
    fnd[0] = digit[minute % 10];

    for (i = 0; i < 4; i++) {
        PORTC = fnd[i];
        PORTG = fnd_sel[i];
        _delay_us(2500);
    }
}

void switch_check() {
	if (Sw1 == TRUE) {
		if (Mode == CLOCK_DISPLAY) {
			Mode = CLOCK_HH_EDIT;
			Sw1 = FALSE;
		}
		else if (Mode == CLOCK_HH_EDIT) {
			Mode = CLOCK_MM_EDIT;
			Sw1 = FALSE;
		}
		else if (Mode == CLOCK_MM_EDIT) {
			Mode = CLOCK_DISPLAY;
			Sw1 = FALSE;
		}
		// *
	}
	if (Sw2 == TRUE) {
		if (Mode == CLOCK_DISPLAY) {
			Mode = TIMER_DISPLAY;
			Sw2 = FALSE;
		}
		else if (Mode == TIMER_DISPLAY) {
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
void change_mode() {
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

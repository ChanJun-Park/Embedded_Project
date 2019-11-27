#include "includes.h"

#define F_CPU	16000000UL	// CPU frequency = 16 Mhz
#include <avr/io.h>
#include <util/delay.h>

#define  TASK_STK_SIZE  OS_TASK_DEF_STK_SIZE
#define  N_TASKS        3

#define CLOCK_DISPLAY 0
#define CLOCK_HH_EDIT 1
#define CLOCK_MM_EDIT 2

#define TIMER_DISPLAY 3
#define TIMER_HH_EDIT 4
#define TIMER_MM_EDIT 5

OS_STK       TaskStk[N_TASKS][TASK_STK_SIZE];
OS_EVENT	  *Mbox;
OS_EVENT	  *Sem;			    // 크리티컬 섹션 보호용 세마포어

volatile INT8U  Mode;		    // 전체 동작 모드 관리 전역변수

volatile INT8U  ClockSCount;	// 시간 카운트 변수

void  ControlTask(void *data);  // 전체 테스크 실행 순서 관리 테스크
void  ClockTask(void *data);	// 시계 모드 관리 테스크
void  ClockDisplayTask(void *data);	// FND에 시간을 출력하는 테스크

int main (void)
{
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

	Mbox = OSMboxCreate((void*)0);
	Sem = OSSemCreaete(1);

	OSTaskCreate(ControlTask, (void *)0, (void *)&TaskStk[0][TASK_STK_SIZE - 1], 0);
	OSTaskCreate(FndTask, (void *)0, (void *)&TaskStk[1][TASK_STK_SIZE - 1], 1);
	OSTaskCreate(FndDisplayTask, (void *)0, (void *)&TaskStk[2][TASK_STK_SIZE - 1], 2);

	OSStart();

	return 0;
}

void ControlTask (void *data)
{

}

void FndTask (void *data)
{

}

void FndDisplayTask (void *data)
{

}

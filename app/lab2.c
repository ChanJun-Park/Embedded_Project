#include "includes.h"

#define F_CPU	16000000UL	// CPU frequency = 16 Mhz

#include <avr/io.h>
#include <avr/interrupt.h>	// interrupt 관련
#include <util/delay.h>

#define  TASK_STK_SIZE  OS_TASK_DEF_STK_SIZE
#define  N_TASKS        7

/* timer1 1024 prescaling의 경우 초 단위 clock 개수*/
#define ONE_SEC -15626

/* Buzzer on,off */
#define ON 1
#define OFF 0

/* timer2 32분주 prescaling 음계 */
#define MUT 0
#define DO 17
#define RE 43
#define MI 66
#define FA 77
#define SOL 97
#define LA 114
#define TI 129
#define UDO 137
#define URE	150
#define UMI 162
#define UFA 167

/* 박자 관련 상수 */
#define REST 2
#define QUVR 7
#define QUTR 15
#define HALF 30
#define ONE 60
#define ONE_HALF 90
#define TWO 120
#define THREE 180
#define FOUR 240

/* 알람 음악 길이 상수 */
#define MELODY_LEN	61

/* 온도관련 상수 */
#define UCHAR unsigned char
#define USHORT unsigned short
#define ATS75_ADDR 0x98
#define ATS75_TEMP_REG 0
#define ATS75_CONFIG_REG 1

/* led를 켜는 기준이 되는 CDS 값 */
/* 1lux = 600, 10LUX = 35, 100LUX = 7 의 값을 참고함.
출처 datasheet/fig.4 https://www.kth.se/social/files/54ef17dbf27654753f437c56/GL5537.pdf */
#define CDS_1LUX 256
#define CDS_10LUX 871
#define CDS_100LUX 995

/* 전체 상태 관리 상수 */
#define CLOCK_DISPLAY 	0
#define CLOCK_HH_EDIT 	1
#define CLOCK_MM_EDIT 	2

#define TIMER_STOP 		3
#define TIMER_MM_EDIT 	4
#define TIMER_SS_EDIT 	5
#define TIMER_COUNT		6
#define TIMER_PAUSE		7
#define TIMER_ALARM		8

#define TEMP_DISPLAY	9
#define LIGHT_DISPLAY 	11

/* FND 관련 배열 */
typedef unsigned char uc;
const uc digit[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x27, 0x7f, 0x6f};
const uc fnd_sel[4] = {0x01, 0x02, 0x04, 0x08};
const uc dot = 0x80;

volatile INT8U state = OFF;
volatile INT8U mel_idx = 0;
volatile INT8U note_idx = 0;

/* Silent Night, Holy Night melody */
const uc melody[] = {SOL, LA, SOL, MI, MUT,
					 SOL, LA, SOL, MI, MUT,
					 URE, URE, TI, MUT,
					 UDO, UDO, SOL, MUT,
					 LA, LA, UDO, TI, LA, MUT,
					 SOL, LA, SOL, MI, MUT,
					 LA, LA, UDO, TI, LA, MUT,
					 SOL, LA, SOL, MI, MUT,
					 URE, URE, UFA, URE, TI, MUT,
					 UDO, UMI, MUT, MUT,
					 UDO, SOL, MI, SOL, FA, RE, MUT,
					 DO, DO, MUT, MUT};

/* Silent Night, Holy Night beat */
const INT16U note[] = {ONE_HALF, HALF, ONE, THREE, REST,
					   ONE_HALF, HALF, ONE, THREE, REST,
					   TWO, ONE, THREE, REST,
					   TWO, ONE, THREE, REST,
					   TWO, ONE, ONE_HALF, HALF, ONE, REST,
					   ONE_HALF, HALF, ONE, THREE, REST,
					   TWO, ONE, ONE_HALF, HALF, ONE, REST,
					   ONE_HALF, HALF, ONE, THREE, REST,
					   TWO, ONE, ONE_HALF, HALF, ONE, REST,
					   THREE, TWO, ONE, REST,
					   ONE, ONE, ONE, ONE_HALF, HALF, ONE, REST,
					   THREE, TWO, ONE, REST};

OS_STK        TaskStk[N_TASKS][TASK_STK_SIZE];
OS_EVENT	  *Sem;			    		// 크리티컬 섹션 보호용 세마포어
OS_EVENT 	  *TimerSem;				// 타이머 테스크 동기화용 세마포어
OS_EVENT      *SwitchToControlSem;		// ControlTask 동기화용 세마포어
OS_FLAG_GRP   *TaskControlFlag;			// ControlTask가 나머지 테스크를 동기화 하기위한 이벤트 플래그
OS_EVENT 	  *TempMbox;				// 온도값을 저장하는 메일박스

volatile INT8U  	Mode;		// 전체 동작 모드 관리 전역변수
volatile BOOLEAN 	Sw1;		// 스위치1 눌림 체크 전역 변수
volatile BOOLEAN 	Sw2;		// 스위치2 눌림 체크 전역 변수

volatile INT32U  ClockSCount;	// 시간 카운트 변수
volatile uc 	 ClockFnd[4];	// 현재 시간 (HH:MM)

volatile INT16U  TimerSCount;	// 타이머 카운트 변수
volatile uc 	 TimerFnd[4];	// 현재 타이머 시간 (MM:SS)

void ControlTask(void *data);  	// 전체 테스크 실행 순서 관리 테스크
void ClockTask(void *data);	   	// 시계 모드 관리 테스크
void TimerAlarmTask(void *data);// 타이머 알람 출력 테스크
void TimerTask(void *data);    	// 타이머 모드 관리 테스크
void TemperatureTask (void *data);			// 온도 측정 테스크
void TemperatureDisplayTask (void *data);	// 온도 출력 테스크
void LightTask(void *data);		// 조명도 관련 테스크

void initialize(void);
void clock_edit(void);
void timer_edit(void);
void calculate_hh_mm(INT32U scount);
void calculate_mm_ss(INT16U scount);
void display_fnd(uc * fnd);
void change_mode(void);
void switch_task(void);

/* 온도 관련 함수 */
void write_twi_1byte_nopreset(UCHAR reg, UCHAR data);
void write_twi_0byte_nopreset(UCHAR reg);
int ReadTemperature(void);

/* 조명도 관련 함수 */
unsigned short read_adc();
void show_adc(unsigned short value);

/* 인터럽트 핸들러 정의 */
// Sw1
ISR(INT4_vect) {
	_delay_ms(50);  // debouncing
	if (PINE == 0x10) return;

	Sw1 = TRUE;
	OSSemPost(SwitchToControlSem);
}

// Sw2
ISR(INT5_vect) {
	INT8U i;
	_delay_ms(50);  // debouncing
	if (PINE == 0x20) return;

	Sw2 = TRUE;
	if (Mode == CLOCK_HH_EDIT || Mode == CLOCK_MM_EDIT) {
		clock_edit();
		Sw2 = FALSE;
	}
	else if (Mode == TIMER_MM_EDIT || Mode == TIMER_SS_EDIT) {
		timer_edit();
		Sw2 = FALSE;
	}
	OSSemPost(SwitchToControlSem);
}

// Timer2 음계 출력
ISR (TIMER2_OVF_vect) {
	if (Mode == TIMER_ALARM) {
		if (melody[mel_idx] == MUT) {
			PORTB = 0x00;
		}
		else {
			if (state == ON) {
				PORTB = 0x00;
				state = OFF;
			}
			else {
				PORTB = 0x10;
				state = ON;
			}
		}
    	TCNT2 = melody[mel_idx];
	}
    else {
		state = OFF;
		PORTB = 0x00;
	}
}

// Timer1 1초마다 실행
ISR(TIMER1_OVF_vect) {
	TCNT1 = ONE_SEC;
	ClockSCount = (ClockSCount + 1) % 86400;
}

int main (void)
{
	OSInit();

	initialize();

	OSTaskCreate(ControlTask, (void *)0, (void *)&TaskStk[0][TASK_STK_SIZE - 1], 0);
	OSTaskCreate(ClockTask, (void *)0, (void *)&TaskStk[1][TASK_STK_SIZE - 1], 1);

	OSTaskCreate(TimerAlarmTask, (void*)0, (void *)&TaskStk[2][TASK_STK_SIZE - 1], 2);
	OSTaskCreate(TimerTask, (void*)0, (void *)&TaskStk[3][TASK_STK_SIZE - 1], 3);

	OSTaskCreate(TemperatureTask, (void*)0, (void *)&TaskStk[4][TASK_STK_SIZE - 1], 4);
	OSTaskCreate(TemperatureDisplayTask, (void*)0, (void *)&TaskStk[5][TASK_STK_SIZE - 1], 5);

	OSTaskCreate(LightTask, (void*)0, (void *)&TaskStk[6][TASK_STK_SIZE - 1], 6);

    sei();		// 전체 인터럽트 허용
	OSStart();

	return 0;
}

/************************************ Task 정의 ****************************************/

// 스위치가 눌림에 따라서 TaskControlFlag값을 적절히 설정하여
// 전체 테스크의 실행 순서를 관리하는 테스크
void ControlTask (void *data)
{
	INT8U err;

	for(;;) {

		// 스위치 눌림 체크
		// 여기서 blocking 되어 다른 Task에게 실행 양도
		OSSemPend(SwitchToControlSem, 0, &err);

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

// 현재 시간을 출력하는 테스크
void ClockTask (void *data)
{
	INT8U err;
	INT8U i;
	INT32U temp_hour;

	for (;;) {
		OSFlagPend(TaskControlFlag, 0x01, OS_FLAG_WAIT_SET_ALL, 0, &err); // no consume
		calculate_hh_mm(ClockSCount);

		PORTA = 0x80;

		// 현재 시간 출력
		// 깜빡거리는 효과를 위해서 0.5초는 정상출력, 0.5초는 FND의 특정 세그먼트를 제외하고 출력
		ClockFnd[2] += dot;
		for (i = 0; i < 50; i++) {
			display_fnd(ClockFnd);
			if (Mode != CLOCK_DISPLAY && Mode != CLOCK_HH_EDIT && Mode != CLOCK_MM_EDIT)
				break;
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
			if (Mode != CLOCK_DISPLAY && Mode != CLOCK_HH_EDIT && Mode != CLOCK_MM_EDIT)
				break;
		}
	}
}

// 타이머가 끝나면 알람을 울리는 테스크. TimerTask보다 우선순위가 높지만
// TiemerSem을 Pend하여 세마포를 획득하는 경우에만 실행되고, 나머지 경우에는
// TimerTask에게 실행을 양도한다.
void TimerAlarmTask(void *data)
{
	INT8U err;

	for(;;) {
		OSSemPend(TimerSem, 0, &err);
		note_idx = 0;
		mel_idx = 0;
		
		TIMSK |= (1 << TOIE2);	// 알람 출력을 위해 timer2 overflow interrupt enabled
		TCNT2 = melody[mel_idx];

		while(Mode == TIMER_ALARM) {
			OSTimeDly(note[note_idx]);	// FND 출력과 음계 출력을 위한 양도
			note_idx = (note_idx + 1) % MELODY_LEN;
			mel_idx = (mel_idx + 1) % MELODY_LEN;
		}
		TIMSK &= ~(1 << TOIE2);	// 알람 종료를 위해 timer2 overflow interrupt disabled
	}
}

// 사용자가 설정한 타이머를 카운트하고 FND에 출력하는 테스크
void TimerTask (void * data)
{
	INT8U err;
	INT8U i;
	volatile BOOLEAN TimesUp = FALSE;

	for(;;) {
		OSFlagPend(TaskControlFlag, 0x02, OS_FLAG_WAIT_SET_ALL, 0, &err); // no consume
		calculate_mm_ss(TimerSCount);

		PORTA = 0x40;

		if (Mode == TIMER_STOP || Mode == TIMER_PAUSE || Mode == TIMER_ALARM) {
			if (TimesUp == TRUE) {
				TimesUp = FALSE;
				OSSemPost(TimerSem);
			}

			TimerFnd[2] += dot;
			for (i = 0; i < 100; i++) {
				display_fnd(TimerFnd);
				if (Mode != TIMER_STOP && Mode != TIMER_PAUSE && Mode != TIMER_ALARM)
					break;
			}
		}
		else if (Mode == TIMER_COUNT) {

			TimerFnd[2] += dot;
			for (i = 0; i < 50 && Mode == TIMER_COUNT; i++) {
				display_fnd(TimerFnd);
			}

			TimerFnd[2] -= dot;
			for (i = 0; i < 50 && Mode == TIMER_COUNT; i++) {
				display_fnd(TimerFnd);
			}

			if (TimerSCount == 0) {
				Mode = TIMER_ALARM;
				TimesUp = TRUE;
			}
			else {
				if (Mode == TIMER_PAUSE)
					continue;
				TimerSCount -= 1;
			}
		}
		else { // if Mode == TIMER_MM_EDIT || Mode == TIMER_SS_EDIT
			TimerFnd[2] += dot;
			for (i = 0; i < 50 && (Mode == TIMER_MM_EDIT || Mode == TIMER_SS_EDIT); i++) {
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
			for (i = 0; i < 50 && (Mode == TIMER_MM_EDIT || Mode == TIMER_SS_EDIT); i++) {
				display_fnd(TimerFnd);
			}
		}
	}
}

// 현재 온도를 계산하는 테스크. 현재 온도값을 계산하여 TempMbox 메일박스에
// 전달하고, TemperatureDisplayTask가 이를 화면에 출력할 수 있도록
// OSTimeDly 를 통해 실행을 양도한다.
void TemperatureTask (void *data)
{
	int	value;
	INT8U err;

	data = data;

	write_twi_1byte_nopreset(ATS75_CONFIG_REG, 0x00);
	write_twi_0byte_nopreset(ATS75_TEMP_REG);
	while (1)  {
		OSFlagPend(TaskControlFlag, 0x04, OS_FLAG_WAIT_SET_ALL, 0 ,&err);  // no consume;
		PORTA = 0x20;

		OS_ENTER_CRITICAL();
		value = ReadTemperature();
		OS_EXIT_CRITICAL();

		OS_ENTER_CRITICAL();
		OSMboxPost(TempMbox, (void*)& value);
		OS_EXIT_CRITICAL();

		OSTimeDly(100);
	}
}

// 현재 온도를 FND에 출력하는 테스크.
void TemperatureDisplayTask (void *data)
{
	INT8U value;
	INT8U err;

	void *temp;
    data = data;

	for(;;) {
		OSFlagPend(TaskControlFlag, 0x04, OS_FLAG_WAIT_SET_ALL, 0 ,&err);  // no consume;
		temp = OSMboxAccept(TempMbox);
		if (temp != (void*)0) {
			value = *(INT8U*)temp;
		}

		PORTC = digit[value % 10];
		PORTG = 0x01;
		_delay_ms(2);
		PORTC = digit[value / 10];
		PORTG = 0x02;
		_delay_ms(2);
	}
}

// 현재 조명값에 따라서 LED를 켜거나 끄는 테스크
void LightTask(void *data)
{
	INT8U err;
	unsigned short value;

    while(1)
    {
		OSFlagPend(TaskControlFlag, 0x08, OS_FLAG_WAIT_SET_ALL, 0 ,&err);  // no consume;
        value = read_adc();
        show_adc(value);
    }
}

/************************************ Task 정의 끝 **************************************/

/************************************ 사용 함수 정의 ************************************/

void initialize(void) {
	INT8U err;

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

	// 시간을 계산하기 위해 타이머 1 분주비 설정
	OS_ENTER_CRITICAL();
	TCCR1B = ((1 << CS12) | (0 << CS11) | (1 << CS10)); // timer1 1024 prescaling
	TIMSK |= (1 << TOIE1);	// timer1 overflow interrupt enabled
	TCNT1 = ONE_SEC;
	OS_EXIT_CRITICAL();
	
	// 음계를 출력하기 위해 타이머 2 분주비 설정
	TCCR2 = ((0 << CS22) | (1 << CS21) | (1 << CS20));	// timer2 clock 32 prescaling

	// buzzer 출력 설정
	DDRB = 0x10;

	// fnd 출력 설정
	DDRC = 0xff;
    DDRG = 0x0f;

	// 디버그용 led 출력 설정
	DDRA = 0xff;

    // 스위치 입력 설정
    DDRE = 0xcf;        // 0b1100 1111
    EICRB = 0x0a;       // 0b0000 1010, falling edge triger
    EIMSK = 0x30;       // 0b0011 0000,

	// 온도 관련 설정
	PORTD = 3; 						// For Pull-up override value
    SFIOR &= ~(1 << PUD); 			// PUD
    TWSR = 0; 						// TWPS0 = 0, TWPS1 = 0
    TWBR = 32;						// for 100  K Hz bus clock
	TWCR = _BV(TWEA) | _BV(TWEN);	// TWEA = Ack pulse is generated
									// TWEN = TWI

	// 조도 센서 관련 설정
	ADMUX = 0x00;
    ADCSRA = 0x87;

	Sem = OSSemCreate(1);
	TimerSem = OSSemCreate(0);
	SwitchToControlSem = OSSemCreate(0);
	TempMbox = OSMboxCreate((void*)0);
	TaskControlFlag = OSFlagCreate(0x01, &err);		// CLOCK_DISPLAY 모드로 시작
}

// 시간을 1 시간 또는 1 분 증가시키는 함수
void clock_edit(void) {
	if (Mode == CLOCK_HH_EDIT) {
		ClockSCount = (ClockSCount + 3600) % 86400;
	}
	else if (Mode == CLOCK_MM_EDIT) {
		INT32U temp_hour = ClockSCount / 3600; 
		ClockSCount = (ClockSCount + 60) % 86400;

		// 분 단위 변경으로 인해 시간 단위가 변경되는 것 방지
		if (temp_hour != ClockSCount / 3600) {
			ClockSCount = temp_hour * 3600;
		}
	}
	calculate_hh_mm(ClockSCount);
}

// 타이머를 1분 또는 1초 증가시키는 함수
void timer_edit(void) {
	if (Mode == TIMER_MM_EDIT) {
		TimerSCount = (TimerSCount + 60) % 6000;
	}
	else if (Mode == TIMER_SS_EDIT) {
		INT16U temp_minute = TimerSCount / 60;
		TimerSCount = (TimerSCount + 1) % 6000;

		// 초 단위 변경으로 인해 분 단위가 변경되는 것 방지
		if (temp_minute != TimerSCount / 60) {
			TimerSCount = temp_minute * 60;
		}
	}
	calculate_mm_ss(TimerSCount);
}

// FND 에 출력할 시간과 분을 계산하여 ClockFnd 배열에 저장하는 함수
void calculate_hh_mm(INT32U scount) {
	INT8U hh, mm;

	hh = scount / 3600;
	mm = (scount % 3600) / 60;

	ClockFnd[3] = digit[hh / 10];
	ClockFnd[2] = digit[hh % 10];
	ClockFnd[1] = digit[mm / 10];
	ClockFnd[0] = digit[mm % 10];
}

// FND 에 출력할 분과 초를 계산하여 TimerFnd 배열에 저장하는 함수
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

// 스위치가 눌림에 따라서 현재 Mode를 변경시키는 함수
void change_mode() {
	if (Sw1 == TRUE) {
		if (Mode == CLOCK_DISPLAY) {
			TIMSK &= ~(1 << TOIE1);	// 시간을 수정하는 동안 시계를 멈춰둔다.
			Mode = CLOCK_HH_EDIT;
		}
		else if (Mode == CLOCK_HH_EDIT) {
			Mode = CLOCK_MM_EDIT;
		}
		else if (Mode == CLOCK_MM_EDIT) {
			TIMSK |= (1 << TOIE1);	// 시간 수정 완료 후 다시 타이머를 작동
			Mode = CLOCK_DISPLAY;
		}
		else if (Mode == TIMER_STOP) {
			Mode = TIMER_MM_EDIT;
		}
		else if (Mode == TIMER_MM_EDIT) {
			Mode = TIMER_SS_EDIT;
		}
		else if (Mode == TIMER_SS_EDIT) {
			Mode = TIMER_COUNT;
		}
		else if (Mode == TIMER_COUNT) {
			Mode = TIMER_PAUSE;
		}
		else if (Mode == TIMER_PAUSE) {
			Mode = TIMER_COUNT;
		}
		else if (Mode == TIMER_ALARM) {
			Mode = TIMER_STOP;
		}
		Sw1 = FALSE;
		return;
	}
	if (Sw2 == TRUE) {
		if (Mode == CLOCK_DISPLAY) {
			Mode = TIMER_STOP;
		}
		else if (Mode == TIMER_PAUSE) {
			Mode = TIMER_STOP;
			TimerSCount = 0;
		}
		else if (Mode == TIMER_ALARM) {
			Mode = TIMER_STOP;
		}
		else if (Mode == TIMER_STOP) {
			Mode = TEMP_DISPLAY;
		}
		else if (Mode == TEMP_DISPLAY) {
			PORTC = 0x00;
			Mode = LIGHT_DISPLAY;
		}
		else if (Mode == LIGHT_DISPLAY) {
			Mode = CLOCK_DISPLAY;
		}
		Sw2 = FALSE;
		return;
	}
}


// 현재 Mode에 알맞는 TaskControlFlag 값을 설정하여
// 적절한 테스크가 실행되도록 하는 함수
void switch_task() {
	INT8U err;

	if (Mode == CLOCK_DISPLAY || Mode == CLOCK_HH_EDIT || Mode == CLOCK_MM_EDIT) {
		OSFlagPost(TaskControlFlag, 0x08, OS_FLAG_CLR, &err);
		OSFlagPost(TaskControlFlag, 0x01, OS_FLAG_SET, &err);
	}
	else if (Mode == TIMER_STOP || Mode == TIMER_SS_EDIT || Mode == TIMER_ALARM ||
			 Mode == TIMER_MM_EDIT || Mode == TIMER_ALARM || Mode == TIMER_PAUSE) {
		OSFlagPost(TaskControlFlag, 0x01, OS_FLAG_CLR, &err);
		OSFlagPost(TaskControlFlag, 0x02, OS_FLAG_SET, &err);
	}
	else if (Mode == TEMP_DISPLAY) {
		OSFlagPost(TaskControlFlag, 0x02, OS_FLAG_CLR, &err);
		OSFlagPost(TaskControlFlag, 0x04, OS_FLAG_SET, &err);
	}
	else if (Mode == LIGHT_DISPLAY) {
		OSFlagPost(TaskControlFlag, 0x04, OS_FLAG_CLR, &err);
		OSFlagPost(TaskControlFlag, 0x08, OS_FLAG_SET, &err);
	}
}

// TWI 출력 관련 함수
void write_twi_1byte_nopreset(UCHAR reg, UCHAR data) {
	TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN); // START
	while (((TWCR & (1 << TWINT)) == 0x00) || ((TWSR & 0xf8) != 0x08 && (TWSR & 0xf8) != 0x10)); // ACK
	TWDR = ATS75_ADDR | 0;  // SLA+W, W=0
	TWCR = (1 << TWINT) | (1 << TWEN);  // SLA+W
	while (((TWCR & (1 << TWINT)) == 0x00) || (TWSR & 0xf8) != 0x18);
	TWDR = reg;    // aTS75 Reg
	TWCR = (1 << TWINT) | (1 << TWEN);  // aTS75 Reg
	while (((TWCR & (1 << TWINT)) == 0x00) || (TWSR & 0xF8) != 0x28);
	TWDR = data;    // DATA
	TWCR = (1 << TWINT) | (1 << TWEN);  // DATA
	while (((TWCR & (1 << TWINT)) == 0x00) || (TWSR & 0xF8) != 0x28);
	TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN); // STOP
}

// TWI 출력 관련 함수
void write_twi_0byte_nopreset(UCHAR reg) {
	TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN); // START
	while (((TWCR & (1 << TWINT)) == 0x00) || ((TWSR & 0xf8) != 0x08 && (TWSR & 0xf8) != 0x10));  // ACK
	TWDR = ATS75_ADDR | 0; // SLA+W, W=0
	TWCR = (1 << TWINT) | (1 << TWEN);  // SLA+W
	while (((TWCR & (1 << TWINT)) == 0x00) || (TWSR & 0xf8) != 0x18);
	TWDR = reg;    // aTS75 Reg
	TWCR = (1 << TWINT) | (1 << TWEN);  // aTS75 Reg
	while (((TWCR & (1 << TWINT)) == 0x00) || (TWSR & 0xF8) != 0x28);
	TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN); // STOP
}

// 온도센서로부터 온도를 읽어서 반환하는 함수
int ReadTemperature(void)
{
	int value;

	TWCR = _BV(TWSTA) | _BV(TWINT) | _BV(TWEN);
	while(!(TWCR & _BV(TWINT)));

	TWDR = 0x98 + 1; //TEMP_I2C_ADDR + 1
	TWCR = _BV(TWINT) | _BV(TWEN);
	while(!(TWCR & _BV(TWINT)));

	TWCR = _BV(TWINT) | _BV(TWEN) | _BV(TWEA);
	while(!(TWCR & _BV(TWINT)));

	value = TWDR << 8;
	TWCR = _BV(TWINT) | _BV(TWEN);
	while(!(TWCR & _BV(TWINT)));

	value |= TWDR;
	TWCR = _BV(TWINT) | _BV(TWEN) | _BV(TWSTO);

	value >>= 8;

	TIMSK = (value >= 33) ? TIMSK | _BV(TOIE2): TIMSK & ~_BV(TOIE2);

	return value;
}

// 현재 조명에 대한 값을 반환하는 함수
unsigned short read_adc()
{
    unsigned char adc_low, adc_high;
    unsigned short value;

    ADCSRA |= 0x40;     // start conversion
    while((ADCSRA & 0x10) != 0x10);

    adc_low = ADCL;
    adc_high = ADCH;
    value = (adc_high << 8) | adc_low;

    return value;
}

// 주어진 조명값에 따라서 LED를 켜는 함수
void show_adc(unsigned short value)
{
	if(value < CDS_1LUX)
		PORTA = 0xff;
    else if (value < CDS_10LUX)
        PORTA = 0xaa;
	else if ( value < CDS_100LUX)
		PORTA = 0x42;
    else
        PORTA = 0x00;
}
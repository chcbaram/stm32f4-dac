#include "swtimer.h"


#ifdef _USE_HW_SWTIMER

typedef struct
{

  bool          timer_en;             // 타이머 인에이블 신호
  SwtimerMode_t timer_mode;           // 타이머 모드
  uint32_t      timer_cnt;            // 현재의 타이머 값
  uint32_t      timer_init;           // 타이머 초기화될때의 카운트 값
  void (*tmr_func)(void *);       // 만료될때 실행될 함수
  void  *tmr_func_arg;              // 함수로 전달할 인수들
} swtimer_t;

static bool is_init = false;
static volatile uint32_t sw_timer_counter      = 0;
static volatile uint16_t sw_timer_handle_index = 0;
static swtimer_t  swtimer_tbl[_HW_DEF_SW_TIMER_MAX];           // 타이머 배열 선언
static TIM_HandleTypeDef htim11;

static void swtimerInitTimer(void);
static void swtimerTimerCallback(TIM_HandleTypeDef *htim);




bool swtimerInit(void)
{
  uint8_t i;


  if (is_init)
  {
    return true;
  }


  // 구조체 초기화
  for(i=0; i<_HW_DEF_SW_TIMER_MAX; i++)
  {
    swtimer_tbl[i].timer_en   = false;
    swtimer_tbl[i].timer_cnt  = 0;
    swtimer_tbl[i].timer_init = 0;
    swtimer_tbl[i].tmr_func   = NULL;
  }

  is_init = true;

  swtimerInitTimer();

  return true;
}

void swtimerInitTimer(void)
{
   __HAL_RCC_TIM11_CLK_ENABLE();


  htim11.Instance = TIM11;
  htim11.Init.Prescaler = 95;
  htim11.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim11.Init.Period = 999;
  htim11.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim11.Init.RepetitionCounter = 0;
  htim11.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim11) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_TIM_RegisterCallback(&htim11, HAL_TIM_PERIOD_ELAPSED_CB_ID, swtimerTimerCallback);

  HAL_NVIC_SetPriority(TIM1_TRG_COM_TIM11_IRQn, 15, 0);
  HAL_NVIC_EnableIRQ(TIM1_TRG_COM_TIM11_IRQn);  

  HAL_TIM_Base_Start_IT(&htim11);
}

void swtimerISR(void)
{
  uint8_t i;


  sw_timer_counter++;


  for (i=0; i<_HW_DEF_SW_TIMER_MAX && i<sw_timer_handle_index; i++)   
  {
    if ( swtimer_tbl[i].timer_en == true)                       
    {
      swtimer_tbl[i].timer_cnt--;                               

      if (swtimer_tbl[i].timer_cnt == 0)                        
      {
        if(swtimer_tbl[i].timer_mode == ONE_TIME)               
        {
          swtimer_tbl[i].timer_en = false;                       
        }

        swtimer_tbl[i].timer_cnt = swtimer_tbl[i].timer_init;   

        (*swtimer_tbl[i].tmr_func)(swtimer_tbl[i].tmr_func_arg);  
      }
    }
  }
}

void swtimerSet(swtimer_handle_t handle, uint32_t period_ms, SwtimerMode_t mode, void (*Fnct)(void *), void *arg)
{
  if(handle < 0) return;

  swtimer_tbl[handle].timer_mode = mode;    
  swtimer_tbl[handle].tmr_func   = Fnct;       
  swtimer_tbl[handle].tmr_func_arg = arg;        
  swtimer_tbl[handle].timer_cnt  = period_ms;
  swtimer_tbl[handle].timer_init = period_ms;
}

void swtimerStart(swtimer_handle_t handle)
{
  if(handle < 0) return;

  swtimer_tbl[handle].timer_cnt = swtimer_tbl[handle].timer_init;
  swtimer_tbl[handle].timer_en  = true;
}

void swtimerStop (swtimer_handle_t handle)
{
  if(handle < 0) return;

  swtimer_tbl[handle].timer_en = false;
}

void swtimerReset(swtimer_handle_t handle)
{
  if(handle < 0) return;

  swtimer_tbl[handle].timer_en   = false;
  swtimer_tbl[handle].timer_cnt  = swtimer_tbl[handle].timer_init;
}

swtimer_handle_t swtimerGetHandle(void)
{
  swtimer_handle_t tmr_index = sw_timer_handle_index;

  if (tmr_index < _HW_DEF_SW_TIMER_MAX)
    sw_timer_handle_index++;
  else
    tmr_index = -1;

  return tmr_index;
}

uint32_t swtimerGetCounter(void)
{
  return sw_timer_counter;
}

void TIM1_TRG_COM_TIM11_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim11);
}

void swtimerTimerCallback(TIM_HandleTypeDef *htim)
{
  swtimerISR();
}

#endif

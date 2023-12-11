#include "button.h"


#ifdef _USE_HW_BUTTON
#include "gpio.h"
#include "cli.h"
#include "swtimer.h"


typedef struct
{
  uint8_t     state;
  bool        pressed;
  uint16_t    pressed_cnt;
  uint32_t    pre_time;
} button_t;



typedef struct
{
  GPIO_TypeDef *port;
  uint32_t      pin;
  GPIO_PinState on_state;
} button_pin_t;



#if CLI_USE(HW_BUTTON)
static void cliButton(cli_args_t *args);
#endif
static bool buttonGetPin(uint8_t ch);
static void buttonISR(void *arg);

static const button_pin_t button_pin[BUTTON_MAX_CH] =
    {
      {GPIOA, GPIO_PIN_2, GPIO_PIN_RESET},  // 0. B1
      {GPIOA, GPIO_PIN_1, GPIO_PIN_RESET},  // 1. B2
    };

static const char *button_name[BUTTON_MAX_CH] = 
{
  "_BTN_B1",   
  "_BTN_B2",   
};

static bool is_enable = true;

static button_t button_tbl[BUTTON_MAX_CH];




bool buttonInit(void)
{
  bool ret = true;
  GPIO_InitTypeDef GPIO_InitStruct = {0};


  __HAL_RCC_GPIOA_CLK_ENABLE();


  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;

  for (int i=0; i<BUTTON_MAX_CH; i++)
  {
    GPIO_InitStruct.Pin = button_pin[i].pin;
    HAL_GPIO_Init(button_pin[i].port, &GPIO_InitStruct);
  }

  for (int i=0; i<BUTTON_MAX_CH; i++)
  {
    button_tbl[i].state          = 0;
    button_tbl[i].pressed_cnt    = 0;
    button_tbl[i].pressed        = false;
  }

  swtimer_handle_t timer_ch;
  timer_ch = swtimerGetHandle();
  if (timer_ch >= 0)
  {
    swtimerSet(timer_ch, 10, LOOP_TIME, buttonISR, NULL);
    swtimerStart(timer_ch);
  }
  else
  {
    logPrintf("[NG] buttonInit()\n     swtimerGetHandle()\n");
  }

#if CLI_USE(HW_BUTTON)
  cliAdd("button", cliButton);
#endif

  return ret;
}

void buttonISR(void *arg)
{
  enum
  {
    BTN_IDLE,
    BTN_DEBOUNCE,
    BTN_PRESSED,
  };

  for (int i=0; i<BUTTON_MAX_CH; i++)
  {
    button_t *p_btn = &button_tbl[i];

    switch(p_btn->state)
    {
      case BTN_IDLE:
        if (buttonGetPin(i) == true)
        {
          p_btn->pre_time = millis();
          p_btn->state = BTN_DEBOUNCE;                    
        }        
        break;

      case BTN_DEBOUNCE:
        if (millis()-p_btn->pre_time >= 50)
        {
          p_btn->pressed = true;
          p_btn->state = BTN_PRESSED;
        }
        else
        {
          if (buttonGetPin(i) == false)
          {
            p_btn->state = BTN_IDLE;    
          }
        }
        break;

      case BTN_PRESSED:
        if (buttonGetPin(i) == false)
        {
          p_btn->state = BTN_IDLE;    
        }      
        break;
    }
  }
}

bool buttonGetPin(uint8_t ch)
{
  bool ret = false;

  if (ch >= BUTTON_MAX_CH)
  {
    return false;
  }

  if (HAL_GPIO_ReadPin(button_pin[ch].port, button_pin[ch].pin) == button_pin[ch].on_state)
  {
    ret = true;
  }

  return ret;
}

void buttonEnable(bool enable)
{
  is_enable = enable;
}

bool buttonGetPressed(uint8_t ch)
{
  if (ch >= BUTTON_MAX_CH || is_enable == false)
  {
    return false;
  }

  return button_tbl[ch].pressed;
}

uint32_t buttonGetData(void)
{
  uint32_t ret = 0;


  for (int i=0; i<BUTTON_MAX_CH; i++)
  {
    ret |= (buttonGetPressed(i)<<i);
  }

  return ret;
}

const char *buttonGetName(uint8_t ch)
{
  ch = constrain(ch, 0, BUTTON_MAX_CH);

  return button_name[ch];
}

uint8_t  buttonGetPressedCount(void)
{
  uint32_t i;
  uint8_t ret = 0;

  for (i=0; i<BUTTON_MAX_CH; i++)
  {
    if (buttonGetPressed(i) == true)
    {
      ret++;
    }
  }

  return ret;
}

uint32_t buttonGetClicked(uint8_t ch, bool reset)
{
  volatile uint32_t ret = 0;

  if (ch >= BUTTON_MAX_CH || is_enable == false) return 0;

  ret = button_tbl[ch].pressed;

  if (reset)
  {
    button_tbl[ch].pressed = false;
  }
  return ret;
}

#if CLI_USE(HW_BUTTON)
void cliButton(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    for (int i=0; i<BUTTON_MAX_CH; i++)
    {
      cliPrintf("%-12s pin %d : %d\n", buttonGetName(i), button_pin[i].pin, buttonGetPressed(i));
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "show"))
  {    
    while(cliKeepLoop())
    {
      for (int i=0; i<BUTTON_MAX_CH; i++)
      {
        cliPrintf("%d", buttonGetPressed(i));
      }
      delay(50);
      cliPrintf("\r");
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "clicked"))
  {    
    while(cliKeepLoop())
    {
      for (int i=0; i<BUTTON_MAX_CH; i++)
      {
        uint32_t clicked_cnt;

        clicked_cnt = buttonGetClicked(i, true);
        if (clicked_cnt > 0)
        {
          cliPrintf("ch %d clicked : %d\n", i, clicked_cnt);
        }
      }
      delay(50);
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("button info\n");
    cliPrintf("button show\n");
    cliPrintf("button clicked\n");
  }
}
#endif



#endif

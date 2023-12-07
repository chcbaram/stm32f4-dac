#include "i2s.h"


#ifdef _USE_HW_I2S
#include "cli.h"
#include "gpio.h"
#include "qbuffer.h"
#include "buzzer.h"
#include "es8156.h"


typedef enum
{
  I2S_BIT_DEPTH_8BIT  = 8,
  I2S_BIT_DEPTH_16BIT = 16,
  I2S_BIT_DEPTH_24BIT = 24,
  I2S_BIT_DEPTH_32BIT = 32,
} I2sBitDepth_t;

typedef struct
{
  int16_t volume;
} i2s_cfg_t;

#define I2S_SAMPLERATE_MAX      I2S_AUDIOFREQ_96K
#define I2S_SAMPLERATE_HZ       I2S_AUDIOFREQ_48K
#define I2S_BUF_MS              (4)
#define I2S_BUF_FRAME_LEN       ((I2S_SAMPLERATE_MAX * 2 * I2S_BUF_MS) / 1000)  // 96Khz, Stereo, 4ms
#define I2S_BUF_CNT             16



#ifdef _USE_HW_CLI
static void cliI2s(cli_args_t *args);
#endif

static bool is_init = false;
static bool is_started = false;
static bool is_busy = false;
static uint32_t i2s_sample_rate = I2S_SAMPLERATE_HZ;
static uint16_t i2s_sample_bytes = 4;
static uint16_t i2s_num_of_ch = 2;

static I2sBitDepth_t i2s_sample_depth = I2S_BIT_DEPTH_24BIT;


static int32_t  i2s_frame_buf[I2S_BUF_FRAME_LEN * 2];
static uint32_t i2s_frame_len = 0;
static int16_t  i2s_volume = 100;
static i2s_cfg_t i2s_cfg;



static qbuffer_t i2s_q;
static int32_t   i2s_q_buf[I2S_BUF_FRAME_LEN * I2S_BUF_CNT];

static I2S_HandleTypeDef hi2s2;
static DMA_HandleTypeDef hdma_spi2_tx;







bool i2sInit(void)
{
  bool ret = true;


  hi2s2.Instance                = SPI2;
  hi2s2.Init.Mode               = I2S_MODE_MASTER_TX;
  hi2s2.Init.Standard           = I2S_STANDARD_PHILIPS;
  hi2s2.Init.DataFormat         = I2S_DATAFORMAT_24B;
  hi2s2.Init.MCLKOutput         = I2S_MCLKOUTPUT_ENABLE;
  hi2s2.Init.AudioFreq          = I2S_SAMPLERATE_HZ;
  hi2s2.Init.CPOL               = I2S_CPOL_LOW;
  hi2s2.Init.ClockSource        = I2S_CLOCK_PLL;
  hi2s2.Init.FullDuplexMode     = I2S_FULLDUPLEXMODE_DISABLE;
  if (HAL_I2S_Init(&hi2s2) != HAL_OK)
  {
    ret = false;
  }

  es8156SetConfig(i2s_sample_rate, i2s_sample_depth);

  qbufferCreateBySize(&i2s_q, (uint8_t *)i2s_q_buf, sizeof(int32_t), I2S_BUF_FRAME_LEN * I2S_BUF_CNT);

  i2s_frame_len = (i2s_sample_rate * 2 * I2S_BUF_MS) / 1000;
  i2s_sample_bytes = hi2s2.Init.DataFormat == I2S_DATAFORMAT_16B ? 2:4;

  i2sCfgLoad();
  i2sStart();

  is_init = ret;

  logPrintf("[%s] i2sInit()\n", ret ? "OK" : "NG");

#ifdef _USE_HW_CLI
  cliAdd("i2s", cliI2s);
#endif

  return ret;
}

bool i2sCfgLoad(void)
{
  bool ret = true;

  i2sSetVolume(i2s_cfg.volume);
  return ret;
}

bool i2sCfgSave(void)
{
  bool ret = true;

  i2s_cfg.volume = i2s_volume;
  return ret;
}

bool i2sIsBusy(void)
{
  return is_busy;
}

bool i2sSetSampleRate(uint32_t freq)
{
  bool ret = true;
  uint32_t frame_len;
  const uint32_t freq_tbl[8] = 
  {
    I2S_AUDIOFREQ_96K,
    I2S_AUDIOFREQ_48K,  
    I2S_AUDIOFREQ_44K,
    I2S_AUDIOFREQ_32K,
    I2S_AUDIOFREQ_22K,
    I2S_AUDIOFREQ_16K,
    I2S_AUDIOFREQ_11K,
    I2S_AUDIOFREQ_8K,
  };

  ret = false;
  for (int i=0; i<8; i++)
  {
    if (freq_tbl[i] == freq)
    {
      ret = true;
      break;
    }
  }
  if (ret != true)
  {
    return false;
  }

  i2sStop();
  

  i2s_sample_rate = freq;
  frame_len = (i2s_sample_rate * 2 * I2S_BUF_MS) / 1000;
  i2s_frame_len = frame_len;

  hi2s2.Init.AudioFreq = freq;
  if (HAL_I2S_Init(&hi2s2) != HAL_OK)
  {
    ret = false;
  }
  i2sStart();

  return ret;
}

uint32_t i2sGetSampleRate(void)
{
  return i2s_sample_rate;
}

bool i2sStart(void)
{
  bool ret = false;
  HAL_StatusTypeDef status;
  I2S_HandleTypeDef *p_i2s = &hi2s2;

  memset(i2s_frame_buf, 0, sizeof(i2s_frame_buf));
  status = HAL_I2S_Transmit_DMA(p_i2s, (uint16_t *)i2s_frame_buf, i2s_frame_len * 2);
  if (status == HAL_OK)
  {
    is_started = true;
  }
  else
  {
    is_started = false;
  }

  return ret;
}

bool i2sStop(void)
{
  is_started = false;
  HAL_I2S_DMAStop(&hi2s2);

  return true;
}

int8_t i2sGetEmptyChannel(void)
{
  return 0;
}

uint32_t i2sGetFrameSize(void)
{
  return i2s_frame_len;
}

uint32_t i2sAvailableForWrite(uint8_t ch)
{
  return qbufferAvailableForWrite(&i2s_q);
}

bool i2sWrite(uint8_t ch, void *p_data, uint32_t samples)
{
  return qbufferWrite(&i2s_q, p_data, samples);
}

// https://m.blog.naver.com/PostView.nhn?blogId=hojoon108&logNo=80145019745&proxyReferer=https:%2F%2Fwww.google.com%2F
//
float i2sGetNoteHz(int8_t octave, int8_t note)
{
  float hz;
  float f_note;

  if (octave < 1) octave = 1;
  if (octave > 8) octave = 8;

  if (note <  1) note = 1;
  if (note > 12) note = 12;

  f_note = (float)(note-10)/12.0f;

  hz = pow(2, (octave-1)) * 55 * pow(2, f_note);

  return hz;
}

// https://gamedev.stackexchange.com/questions/4779/is-there-a-faster-sine-function
//
float i2sSin(float x)
{
  const float B = 4 / M_PI;
  const float C = -4 / (M_PI * M_PI);

  return -(B * x + C * x * ((x < 0) ? -x : x));
}

bool i2sPlayBeep(uint32_t freq_hz, uint16_t volume, uint32_t time_ms)
{
  uint32_t pre_time;
  int32_t sample_rate = i2s_sample_rate;
  int32_t num_samples = i2s_frame_len;
  float sample_point;
  int32_t sample[num_samples];
  int16_t sample_index = 0;
  float div_freq;
  int8_t mix_ch;
  int32_t volume_out;
  uint32_t sample_num;
  uint32_t sample_num_max;


  if (time_ms == 0)
    return true;


  volume = constrain(volume, 0, 100);
  volume_out = (INT16_MAX/40) * volume / 100;

  mix_ch =  i2sGetEmptyChannel();

  div_freq = (float)sample_rate/(float)freq_hz;
  sample_num_max = (sample_rate/1000) * time_ms * i2s_num_of_ch;

  sample_num = 0;
  pre_time = millis();
  while(millis()-pre_time <= time_ms)
  {
    if (i2sAvailableForWrite(mix_ch) >= num_samples)
    {
      if (sample_num < sample_num_max)
      {
        for (int i=0; i<num_samples; i+=2)
        {
          sample_point = i2sSin(2.0f * M_PI * (float)(sample_index) / ((float)div_freq));
          sample[i + 0] = (int32_t)(sample_point * volume_out);
          sample[i + 1] = (int32_t)(sample_point * volume_out);
          sample_index = (sample_index + 1) % (int)(div_freq);
        }
        i2sWrite(mix_ch, sample, num_samples);
        sample_num += num_samples;
      }
    }  
  }
  logPrintf("%d/%d ms\n", millis()-pre_time, time_ms);
  return true;
}

int16_t i2sGetVolume(void)
{
  return i2s_volume;
}

bool i2sSetVolume(int16_t volume)
{
  volume = constrain(volume, 0, 100);
  i2s_volume = volume;

  return true;
}

bool i2sSetBitDepth(I2sBitDepth_t bit_depth)
{
  return true;
}

bool i2sGetBitDepth(I2sBitDepth_t *bit_depth)
{
  return true;
}

void i2sUpdateBuffer(uint8_t index)
{
  if (qbufferAvailable(&i2s_q) >= i2s_frame_len)
  {    
    qbufferRead(&i2s_q, (uint8_t *)&i2s_frame_buf[index * i2s_frame_len], i2s_frame_len);
    is_busy = true;
  }
  else
  {
    memset(&i2s_frame_buf[index * i2s_frame_len], 0, i2s_frame_len * i2s_sample_bytes);
    is_busy = false;
  }
}

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
  i2sUpdateBuffer(0);
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s)
{
  i2sUpdateBuffer(1);
}

void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *hi2s)
{
  return;
}

void SPI2_IRQHandler(void)
{
  HAL_I2S_IRQHandler(&hi2s2);
}

void DMA1_Stream4_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_spi2_tx);
}

void HAL_I2S_MspInit(I2S_HandleTypeDef* i2sHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  
  if(i2sHandle->Instance==SPI2)
  {
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();

    /* I2S2 clock enable */
    __HAL_RCC_SPI2_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**I2S2 GPIO Configuration
    PA3     ------> I2S2_MCK
    PB12     ------> I2S2_WS
    PB13     ------> I2S2_CK
    PB15     ------> I2S2_SD
    */
    GPIO_InitStruct.Pin = GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* I2S2 DMA Init */
    /* SPI2_TX Init */
    hdma_spi2_tx.Instance       = DMA1_Stream4;
    hdma_spi2_tx.Init.Channel   = DMA_CHANNEL_0;
    hdma_spi2_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_spi2_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_spi2_tx.Init.MemInc    = DMA_MINC_ENABLE;
    hdma_spi2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_spi2_tx.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_spi2_tx.Init.Mode      = DMA_CIRCULAR;
    hdma_spi2_tx.Init.Priority  = DMA_PRIORITY_LOW;
    hdma_spi2_tx.Init.FIFOMode  = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_spi2_tx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(i2sHandle,hdmatx,hdma_spi2_tx);

    /* I2S1 interrupt Init */
    HAL_NVIC_SetPriority(SPI2_IRQn, 4, 0);
    HAL_NVIC_EnableIRQ(SPI2_IRQn);

    /* DMA1_Stream3_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 4, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);    
  }
}

void HAL_I2S_MspDeInit(I2S_HandleTypeDef* i2sHandle)
{

  if(i2sHandle->Instance==SPI2)
  {
    /* Peripheral clock disable */
    __HAL_RCC_SPI2_CLK_DISABLE();

    /**I2S2 GPIO Configuration
    PA3     ------> I2S2_MCK
    PB12     ------> I2S2_WS
    PB13     ------> I2S2_CK
    PB15     ------> I2S2_SD
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_3);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_15);

    /* I2S2 DMA DeInit */
    HAL_DMA_DeInit(i2sHandle->hdmatx);

    /* I2S2 interrupt Deinit */
    HAL_NVIC_DisableIRQ(SPI2_IRQn);    
  }
}



#ifdef _USE_HW_CLI
void cliI2s(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info") == true)
  {

    cliPrintf("i2s init      : %d\n", is_init);
    cliPrintf("i2s rate      : %d Khz\n", i2s_sample_rate/1000);
    cliPrintf("i2s buf ms    : %d ms\n", I2S_BUF_MS);
    cliPrintf("i2s frame len : %d \n", i2s_frame_len);
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "beep") == true)
  {
    uint32_t freq;
    uint32_t time_ms;

    freq = args->getData(1);
    time_ms = args->getData(2);
    
    i2sPlayBeep(freq, 100, time_ms);

    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "melody"))
  {
    uint16_t melody[] = {NOTE_C4, NOTE_G3, NOTE_G3, NOTE_A3, NOTE_G3, 0, NOTE_B3, NOTE_C4};
    int note_durations[] = { 4, 8, 8, 4, 4, 4, 4, 4 };

    uint32_t pre_time = millis();
    for (int i=0; i<8; i++) 
    {
      int note_duration = 1000 / note_durations[i];

      i2sPlayBeep(melody[i], 100, note_duration);
      delay(note_duration * 0.3);    
    }
    logPrintf("%d ms\n", millis()-pre_time);
    ret = true;
  }

  if (ret != true)
  {
    cliPrintf("i2s info\n");
    cliPrintf("i2s melody\n");
    cliPrintf("i2s beep freq time_ms\n");
  }
}
#endif

#endif
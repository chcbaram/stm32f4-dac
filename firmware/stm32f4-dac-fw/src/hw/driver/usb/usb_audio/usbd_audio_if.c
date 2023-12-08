/**
  ******************************************************************************
  * @file    USB_Device/AUDIO_Standalone/Src/usbd_audio_if.c
  * @author  MCD Application Team
  * @brief   USB Device Audio interface file.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2017 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------ */
#include "usbd_audio_if.h"
#include "i2s.h"


/* Private typedef ----------------------------------------------------------- */
/* Private define ------------------------------------------------------------ */
/* Private macro ------------------------------------------------------------- */
/* Private function prototypes ----------------------------------------------- */
static int8_t Audio_Init(uint32_t AudioFreq, uint32_t Volume, uint32_t options);
static int8_t Audio_DeInit(uint32_t options);
static int8_t Audio_PlaybackCmd(uint8_t * pbuf, uint32_t size, uint8_t cmd);
static int8_t Audio_VolumeCtl(uint8_t vol);
static int8_t Audio_MuteCtl(uint8_t cmd);
static int8_t Audio_PeriodicTC(uint8_t *pbuf, uint32_t size, uint8_t cmd);
static int8_t Audio_GetState(void);
static int8_t Audio_Receive(uint8_t *pbuf, uint32_t size);
static int8_t Audio_GetBufferLevel(uint8_t *percent);


/* Private variables --------------------------------------------------------- */
extern USBD_HandleTypeDef USBD_Device;
USBD_AUDIO_ItfTypeDef USBD_AUDIO_fops = {
  Audio_Init,
  Audio_DeInit,
  Audio_PlaybackCmd,
  Audio_VolumeCtl,
  Audio_MuteCtl,
  Audio_PeriodicTC,
  Audio_GetState,
  Audio_Receive,
  Audio_GetBufferLevel,
};


volatile static bool is_rx_full = false;
static uint8_t sai_ch = 0;
static bool is_init = false;
static bool is_mute = false;
static void     (*receive_func)(int16_t *p_data, uint32_t samples) = NULL;

/* Private functions --------------------------------------------------------- */

/**
  * @brief  Initializes the AUDIO media low layer.
  * @param  AudioFreq: Audio frequency used to play the audio stream.
  * @param  Volume: Initial volume level (from 0 (Mute) to 100 (Max))
  * @param  options: Reserved for future use
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t Audio_Init(uint32_t AudioFreq, uint32_t Volume, uint32_t options)
{
  logPrintf("Audio_Init()\n");
  logPrintf("  AudioFreq : %d\n", AudioFreq);
  logPrintf("  Volume    : %d\n", Volume);
  logPrintf("  options   : %d\n", options);

  i2sSetSampleRate(AudioFreq);

  if (options > 0)
  {
    is_init = true; 
  }
  return 0;
}

uint8_t Audio_Sof(USBD_HandleTypeDef *pdev)
{
  return (uint8_t)USBD_OK;
}

/**
  * @brief  De-Initializes the AUDIO media low layer.
  * @param  options: Reserved for future use
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t Audio_DeInit(uint32_t options)
{
  logPrintf("Audio_DeInit(%d)\n", options);

  is_init = false; 
  return 0;
}

bool Audio_IsInit(void)
{
  return is_init;
}

void Audio_SetReceiveFunc(void (*func)(int16_t *p_data, uint32_t samples))
{
  receive_func = func;
}

/**
  * @brief  Handles AUDIO command.
  * @param  pbuf: Pointer to buffer of data to be sent
  * @param  size: Number of data to be sent (in bytes)
  * @param  cmd: Command opcode
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t Audio_PlaybackCmd(uint8_t * pbuf, uint32_t size, uint8_t cmd)
{
  switch (cmd)
  {
    case AUDIO_CMD_START:
      if (is_mute == false)
        i2sMute(false);
      break;

    case AUDIO_CMD_STOP:
      i2sMute(true);
      break;

    default:
      break;
  }

  return 0;
}

/**
  * @brief  Controls AUDIO Volume.
  * @param  vol: Volume level (0..100)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t Audio_VolumeCtl(uint8_t vol)
{
  // logPrintf("Audio_VolumeCtl()\n");
  // logPrintf("  vol : %d\n", vol);

  i2sSetVolume(vol);
  
  return 0;
}

/**
  * @brief  Controls AUDIO Mute.
  * @param  cmd: Command opcode
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t Audio_MuteCtl(uint8_t cmd)
{
  // logPrintf("Audio_MuteCtl()\n");
  // logPrintf("  cmd : %d\n", cmd);

  i2sMute(cmd);
  is_mute = cmd;

  return 0;
}

/**
  * @brief  Audio_PeriodicTC
  * @param  cmd: Command opcode
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t Audio_PeriodicTC(uint8_t *pbuf, uint32_t size, uint8_t cmd)
{
  UNUSED(pbuf);
  UNUSED(size);
  UNUSED(cmd);

  return (0);
}

/**
  * @brief  Gets AUDIO State.
  * @param  None
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t Audio_GetState(void)
{
  return 0;
}

int8_t Audio_Receive(uint8_t *pbuf, uint32_t size)
{
  i2sWriteBytes(sai_ch, pbuf, size);


  if (receive_func != NULL)
  {
    receive_func((int16_t *)pbuf, size/2);
  }
  return (int8_t)USBD_OK;
}

static int8_t Audio_GetBufferLevel(uint8_t *percent)
{
  uint16_t total_len;
  uint16_t empty_len;
  uint8_t  buf_level;

  empty_len = i2sAvailableForWrite(sai_ch);
  total_len = empty_len + i2sAvailableForRead(sai_ch);

  buf_level = empty_len *100 / total_len;
  *percent = buf_level;

  return (int8_t)USBD_OK;
}
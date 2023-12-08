/**
  ******************************************************************************
  * @file    usbd_audio.c
  * @author  MCD Application Team
  * @brief   This file provides the Audio core functions.
  *
  *
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  * @verbatim
  *
  *          ===================================================================
  *                                AUDIO Class  Description
  *          ===================================================================
  *           This driver manages the Audio Class 1.0 following the "USB Device Class Definition for
  *           Audio Devices V1.0 Mar 18, 98".
  *           This driver implements the following aspects of the specification:
  *             - Device descriptor management
  *             - Configuration descriptor management
  *             - Standard AC Interface Descriptor management
  *             - 1 Audio Streaming Interface (with single channel, PCM, Stereo mode)
  *             - 1 Audio Streaming Endpoint
  *             - 1 Audio Terminal Input (1 channel)
  *             - Audio Class-Specific AC Interfaces
  *             - Audio Class-Specific AS Interfaces
  *             - AudioControl Requests: only SET_CUR and GET_CUR requests are supported (for Mute)
  *             - Audio Feature Unit (limited to Mute control)
  *             - Audio Synchronization type: Asynchronous
  *             - Single fixed audio sampling rate (configurable in usbd_conf.h file)
  *          The current audio class version supports the following audio features:
  *             - Pulse Coded Modulation (PCM) format
  *             - sampling rate: 48KHz.
  *             - Bit resolution: 16
  *             - Number of channels: 2
  *             - No volume control
  *             - Mute/Unmute capability
  *             - Asynchronous Endpoints
  *
  * @note     In HS mode and when the DMA is used, all variables and data structures
  *           dealing with the DMA during the transaction process should be 32-bit aligned.
  *
  *
  *  @endverbatim
  ******************************************************************************
  */

/* BSPDependencies
- "stm32xxxxx_{eval}{discovery}.c"
- "stm32xxxxx_{eval}{discovery}_io.c"
- "stm32xxxxx_{eval}{discovery}_audio.c"
EndBSPDependencies */

/* Includes ------------------------------------------------------------------*/
#include "usbd_audio.h"
#include "usbd_ctlreq.h"
#include "cli.h"
#include "i2s.h"

/** @addtogroup STM32_USB_DEVICE_LIBRARY
  * @{
  */


/** @defgroup USBD_AUDIO
  * @brief usbd core module
  * @{
  */

/** @defgroup USBD_AUDIO_Private_TypesDefinitions
  * @{
  */
/**
  * @}
  */


/** @defgroup USBD_AUDIO_Private_Defines
  * @{
  */
/**
  * @}
  */


/** @defgroup USBD_AUDIO_Private_Macros
  * @{
  */
#define AUDIO_SAMPLE_FREQ(frq) \
  (uint8_t)(frq), (uint8_t)((frq >> 8)), (uint8_t)((frq >> 16))

#define AUDIO_PACKET_SZE(frq) \
  (uint8_t)(((frq * 2U * USBD_AUDIO_BIT_BYTES) / 1000U) & 0xFFU), (uint8_t)((((frq * 2U * USBD_AUDIO_BIT_BYTES) / 1000U) >> 8) & 0xFFU)

#define AUDIO_PACKET_SZE_24B(frq) (uint8_t)(((frq / 1000U + 1) * 2U * 3U) & 0xFFU), \
                                  (uint8_t)((((frq / 1000U + 1) * 2U * 3U) >> 8) & 0xFFU)


#define USB_SOF_NUMBER() ((((USB_OTG_DeviceTypeDef *)((uint32_t )USB_OTG_HS + USB_OTG_DEVICE_BASE))->DSTS&USB_OTG_DSTS_FNSOF)>>USB_OTG_DSTS_FNSOF_Pos)


#define USBD_AUDIO_LOG     1

#if (USBD_AUDIO_LOG > 0)
#define AUDIO_Log(...) logPrintf(__VA_ARGS__);
#else
#define AUDIO_Log(...)   
#endif 


/**
  * @}
  */


/** @defgroup USBD_AUDIO_Private_FunctionPrototypes
  * @{
  */
static uint8_t USBD_AUDIO_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_AUDIO_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);

static uint8_t USBD_AUDIO_Setup(USBD_HandleTypeDef *pdev,
                                USBD_SetupReqTypedef *req);
static uint8_t *USBD_AUDIO_GetCfgDesc(uint16_t *length);
static uint8_t *USBD_AUDIO_GetDeviceQualifierDesc(uint16_t *length);
static uint8_t USBD_AUDIO_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_AUDIO_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_AUDIO_EP0_RxReady(USBD_HandleTypeDef *pdev);
static uint8_t USBD_AUDIO_EP0_TxReady(USBD_HandleTypeDef *pdev);
static uint8_t USBD_AUDIO_SOF(USBD_HandleTypeDef *pdev);
static void *USBD_AUDIO_GetAudioHeaderDesc(uint8_t *pConfDesc);
static uint8_t USBD_AUDIO_IsoINIncomplete(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_AUDIO_IsoOutIncomplete(USBD_HandleTypeDef *pdev, uint8_t epnum);

static void AUDIO_REQ_GetCurrent(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static void AUDIO_REQ_SetCurrent(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static void AUDIO_REQ_GetMax(USBD_HandleTypeDef* pdev, USBD_SetupReqTypedef* req);
static void AUDIO_REQ_GetMin(USBD_HandleTypeDef* pdev, USBD_SetupReqTypedef* req);
static void AUDIO_REQ_GetRes(USBD_HandleTypeDef* pdev, USBD_SetupReqTypedef* req);
static void AUDIO_OUT_Restart(USBD_HandleTypeDef* pdev);
static void AUDIO_OUT_Stop(USBD_HandleTypeDef* pdev);

static uint8_t  AUDIO_SendFeedbackFreq(USBD_HandleTypeDef *pdev);
static uint32_t AUDIO_GetFeedbackValue(uint32_t rate);
static int32_t  AUDIO_Get_Vol3dB_Shift(int16_t volume);
static int32_t  AUDIO_Volume_Ctrl(int32_t sample, int32_t shift_3dB);
static uint8_t  AUDIO_UpdateFeedbackFreq(USBD_HandleTypeDef *pdev);

static void cliCmd(cli_args_t *args);


/**
  * @}
  */

/** @defgroup USBD_AUDIO_Private_Variables
  * @{
  */

USBD_ClassTypeDef USBD_AUDIO =
{
  USBD_AUDIO_Init,
  USBD_AUDIO_DeInit,
  USBD_AUDIO_Setup,
  USBD_AUDIO_EP0_TxReady,
  USBD_AUDIO_EP0_RxReady,
  USBD_AUDIO_DataIn,
  USBD_AUDIO_DataOut,
  USBD_AUDIO_SOF,
  USBD_AUDIO_IsoINIncomplete,
  USBD_AUDIO_IsoOutIncomplete,
  USBD_AUDIO_GetCfgDesc,
  USBD_AUDIO_GetCfgDesc,
  USBD_AUDIO_GetCfgDesc,
  USBD_AUDIO_GetDeviceQualifierDesc,
};


#define USB_AUDIO_CONFIG_DESC_SIZ_ADD     (USB_AUDIO_CONFIG_DESC_SIZ + 9)

/* USB AUDIO device Configuration Descriptor */
__ALIGN_BEGIN static uint8_t USBD_AUDIO_CfgDesc[USB_AUDIO_CONFIG_DESC_SIZ_ADD] __ALIGN_END =
{
  /* Configuration 1 */
  0x09,                                 /* bLength */
  USB_DESC_TYPE_CONFIGURATION,          /* bDescriptorType */
  LOBYTE(USB_AUDIO_CONFIG_DESC_SIZ_ADD),    /* wTotalLength */
  HIBYTE(USB_AUDIO_CONFIG_DESC_SIZ_ADD),
  0x02,                                 /* bNumInterfaces */
  0x01,                                 /* bConfigurationValue */
  0x00,                                 /* iConfiguration */
#if (USBD_SELF_POWERED == 1U)
  0xC0,                                 /* bmAttributes: Bus Powered according to user configuration */
#else
  0x80,                                 /* bmAttributes: Bus Powered according to user configuration */
#endif /* USBD_SELF_POWERED */
  USBD_MAX_POWER,                       /* MaxPower (mA) */
  /* 09 byte*/

  /* USB Speaker Standard interface descriptor */
  AUDIO_INTERFACE_DESC_SIZE,            /* bLength */
  USB_DESC_TYPE_INTERFACE,              /* bDescriptorType */
  0x00,                                 /* bInterfaceNumber */
  0x00,                                 /* bAlternateSetting */
  0x00,                                 /* bNumEndpoints */
  USB_DEVICE_CLASS_AUDIO,               /* bInterfaceClass */
  AUDIO_SUBCLASS_AUDIOCONTROL,          /* bInterfaceSubClass */
  AUDIO_PROTOCOL_UNDEFINED,             /* bInterfaceProtocol */
  0x00,                                 /* iInterface */
  /* 09 byte*/

  /* USB Speaker Class-specific AC Interface Descriptor */
  AUDIO_INTERFACE_DESC_SIZE,            /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_CONTROL_HEADER,                 /* bDescriptorSubtype */
  0x00,          /* 1.00 */             /* bcdADC */
  0x01,
  0x27,                                 /* wTotalLength */
  0x00,
  0x01,                                 /* bInCollection */
  0x01,                                 /* baInterfaceNr */
  /* 09 byte*/

  /* USB Speaker Input Terminal Descriptor */
  AUDIO_INPUT_TERMINAL_DESC_SIZE,       /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_CONTROL_INPUT_TERMINAL,         /* bDescriptorSubtype */
  0x01,                                 /* bTerminalID */
  0x01,                                 /* wTerminalType AUDIO_TERMINAL_USB_STREAMING   0x0101 */
  0x01,
  0x00,                                 /* bAssocTerminal */
  0x02,                                 /* bNrChannels */
  0x03,                                 /* wChannelConfig 0x0003  Stero */
  0x00,
  0x00,                                 /* iChannelNames */
  0x00,                                 /* iTerminal */
  /* 12 byte*/

  /* USB Speaker Audio Feature Unit Descriptor */
  0x09,                                 /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_CONTROL_FEATURE_UNIT,           /* bDescriptorSubtype */
  AUDIO_OUT_STREAMING_CTRL,             /* bUnitID */
  0x01,                                 /* bSourceID */
  0x01,                                 /* bControlSize */
  AUDIO_CONTROL_MUTE|AUDIO_CONTROL_VOL, /* bmaControls(0) */
  0,                                    /* bmaControls(1) */
  0x00,                                 /* iTerminal */
  /* 09 byte */

  /* USB Speaker Output Terminal Descriptor */
  0x09,      /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_CONTROL_OUTPUT_TERMINAL,        /* bDescriptorSubtype */
  0x03,                                 /* bTerminalID */
  0x01,                                 /* wTerminalType  0x0301 */
  0x03,
  0x00,                                 /* bAssocTerminal */
  0x02,                                 /* bSourceID */
  0x00,                                 /* iTerminal */
  /* 09 byte */

  /* USB Speaker Standard AS Interface Descriptor - Audio Streaming Zero Bandwidth */
  /* Interface 1, Alternate Setting 0                                              */
  AUDIO_INTERFACE_DESC_SIZE,            /* bLength */
  USB_DESC_TYPE_INTERFACE,              /* bDescriptorType */
  0x01,                                 /* bInterfaceNumber */
  0x00,                                 /* bAlternateSetting */
  0x00,                                 /* bNumEndpoints */
  USB_DEVICE_CLASS_AUDIO,               /* bInterfaceClass */
  AUDIO_SUBCLASS_AUDIOSTREAMING,        /* bInterfaceSubClass */
  AUDIO_PROTOCOL_UNDEFINED,             /* bInterfaceProtocol */
  0x00,                                 /* iInterface */
  /* 09 byte*/

  /* USB Speaker Standard AS Interface Descriptor - Audio Streaming Operational */
  /* Interface 1, Alternate Setting 1                                           */
  AUDIO_INTERFACE_DESC_SIZE,            /* bLength */
  USB_DESC_TYPE_INTERFACE,              /* bDescriptorType */
  0x01,                                 /* bInterfaceNumber */
  0x01,                                 /* bAlternateSetting */
  0x02,                                 /* bNumEndpoints, 1 output & 1 feedback */
  USB_DEVICE_CLASS_AUDIO,               /* bInterfaceClass */
  AUDIO_SUBCLASS_AUDIOSTREAMING,        /* bInterfaceSubClass */
  AUDIO_PROTOCOL_UNDEFINED,             /* bInterfaceProtocol */
  0x00,                                 /* iInterface */
  /* 09 byte*/

  /* USB Speaker Audio Streaming Interface Descriptor */
  AUDIO_STREAMING_INTERFACE_DESC_SIZE,  /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_STREAMING_GENERAL,              /* bDescriptorSubtype */
  0x01,                                 /* bTerminalLink */
  0x01,                                 /* bDelay */
  0x01,                                 /* wFormatTag AUDIO_FORMAT_PCM  0x0001 */
  0x00,
  /* 07 byte*/

  /* USB Speaker Audio Type I Format Interface Descriptor */
  0x0B,                                 /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_STREAMING_FORMAT_TYPE,          /* bDescriptorSubtype */
  AUDIO_FORMAT_TYPE_I,                  /* bFormatType */
  2,                                    /* bNrChannels */
  USBD_AUDIO_BIT_BYTES,                 /* bSubFrameSize :  3 Bytes per frame (24bits) */
  USBD_AUDIO_BIT_LEN,                   /* bBitResolution (24-bits per sample) */
  1,                                    /* bSamFreqType only one frequency supported */
  AUDIO_SAMPLE_FREQ(USBD_AUDIO_FREQ),   /* Audio sampling frequency coded on 3 bytes */
  /* 11 byte*/

  /* Standard AS Isochronous Audio Data Endpoint Descriptor */
  AUDIO_STANDARD_ENDPOINT_DESC_SIZE,    /* bLength */
  USB_DESC_TYPE_ENDPOINT,               /* bDescriptorType */
  AUDIO_OUT_EP,                         /* bEndpointAddress 1 out endpoint */
  USBD_EP_TYPE_ISOC_ASYNC,              /* bmAttributes */
  AUDIO_PACKET_SZE(USBD_AUDIO_FREQ_MAX),/* wMaxPacketSize in Bytes (Freq(Samples)*2(Stereo)*3(HalfWord)) */
  0x01,                                 /* bInterval */
  0x00,                                 /* bRefresh */
  AUDIO_IN_EP,                          /* bSynchAddress */
  /* 09 byte*/

  /* Class-Specific AS Isochronous Audio Data Endpoint Descriptor */
  AUDIO_STREAMING_ENDPOINT_DESC_SIZE,   /* bLength */
  AUDIO_ENDPOINT_DESCRIPTOR_TYPE,       /* bDescriptorType */
  AUDIO_ENDPOINT_GENERAL,               /* bDescriptorSubtype */
  0x01,                                 /* bmAttributes - Sampling Frequency control is supported. See UAC Spec 1.0 p.62 */
  0x00,                                 /* bLockDelayUnits */
  0x00,                                 /* wLockDelay */
  0x00,
  /* 07 byte*/

  // Standard Descriptor - See UAC Spec 1.0 p.63 4.6.2.1 Standard AS Isochronous Synch Endpoint Descriptor
	// 3byte 10.14 sampling frequency feedback to host
  AUDIO_STANDARD_ENDPOINT_DESC_SIZE, /* bLength */
  USB_DESC_TYPE_ENDPOINT,            /* bDescriptorType */
  AUDIO_IN_EP,                       /* bEndpointAddress */
  USBD_EP_TYPE_ISOC,                 /* bmAttributes */
  0x03,                              /* wMaxPacketSize in Bytes */
  0x00,
  0x01,                              /* bInterval 1ms */
  SOF_RATE,                          /* bRefresh 4ms = 2^2 */
  0x00,                              /* bSynchAddress */
  // 09 byte

} ;

/* USB Standard Device Descriptor */
__ALIGN_BEGIN static uint8_t USBD_AUDIO_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
{
  USB_LEN_DEV_QUALIFIER_DESC,
  USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00,
  0x02,
  0x00,
  0x00,
  0x00,
  0x40,
  0x01,
  0x00,
};

volatile static bool is_init  = false;
volatile static uint32_t rx_count = 0;
volatile static uint32_t rx_rate = 0;
volatile USBD_HandleTypeDef *p_usb_dev = NULL;

enum
{
  DATA_RATE_ISO_IN_INCOMPLETE,
  DATA_RATE_ISO_OUT_INCOMPLETE,
  DATA_RATE_DATA_IN,
  DATA_RATE_DATA_OUT,
  DATA_RATE_FEEDBACK,
  DATA_RATE_MAX
};

volatile static uint32_t data_in_count[DATA_RATE_MAX] = {0, };
volatile static uint32_t data_in_rate[DATA_RATE_MAX] = {0, };


/**
  * @}
  */

/** @defgroup USBD_AUDIO_Private_Functions
  * @{
  */

/**
  * @brief  USBD_AUDIO_Init
  *         Initialize the AUDIO interface
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t USBD_AUDIO_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);
  USBD_AUDIO_HandleTypeDef *haudio;

  /* Allocate Audio structure */
  haudio = (USBD_AUDIO_HandleTypeDef *)USBD_malloc(sizeof(USBD_AUDIO_HandleTypeDef));

  if (haudio == NULL)
  {
    pdev->pClassDataCmsit[pdev->classId] = NULL;
    return (uint8_t)USBD_EMEM;
  }

  pdev->pClassDataCmsit[pdev->classId] = (void *)haudio;
  pdev->pClassData = pdev->pClassDataCmsit[pdev->classId];

  AUDIO_Log("USBD_AUDIO_Init() - IN\n");

  /* Open EP OUT */
  USBD_LL_OpenEP(pdev, AUDIO_OUT_EP, USBD_EP_TYPE_ISOC, AUDIO_OUT_PACKET);
  pdev->ep_out[AUDIO_OUT_EP & 0xFU].is_used = 1U;

  /* Open EP IN */
  USBD_LL_OpenEP(pdev, AUDIO_IN_EP, USBD_EP_TYPE_ISOC, AUDIO_IN_PACKET);
  pdev->ep_in[AUDIO_IN_EP & 0xFU].is_used = 1U;

  /* Flush feedback endpoint */
  USBD_LL_FlushEP(pdev, AUDIO_IN_EP);



  haudio->alt_setting = 0U;
  haudio->offset = AUDIO_OFFSET_UNKNOWN;
  haudio->wr_ptr = 0U;
  haudio->rd_ptr = 0U;
  haudio->rd_enable = 0U;
  haudio->volume = USBD_AUDIO_VOL_DEFAULT;
  haudio->vol_3dB_shift = AUDIO_Get_Vol3dB_Shift(haudio->volume);
  haudio->volume_percent = AUDIO_Volume_Ctrl(100, haudio->vol_3dB_shift/2);  
  haudio->freq = USBD_AUDIO_FREQ;
  haudio->bit_depth = USBD_AUDIO_BIT_BYTES;
  haudio->fb_normal = AUDIO_GetFeedbackValue(USBD_AUDIO_FREQ);
  haudio->fb_target = haudio->fb_normal;

  /* Initialize the Audio output Hardware layer */
  if (((USBD_AUDIO_ItfTypeDef *)pdev->pUserData[pdev->classId])->Init(USBD_AUDIO_FREQ,
                                                                      haudio->volume_percent,
                                                                      0U) != 0U)
  {
    return (uint8_t)USBD_FAIL;
  }  

  p_usb_dev = pdev;

  static bool is_cli = false;
  if (is_cli == false)
  {
    is_cli = true;
    cliAdd("usb-audio", cliCmd);
  }

  AUDIO_Log("USBD_AUDIO_Init() - OUT\n");
  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_AUDIO_DeInit
  *         DeInitialize the AUDIO layer
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t USBD_AUDIO_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);

  is_init = false;
  AUDIO_Log("USBD_AUDIO_DeInit()\n");

  /* Flush all endpoints */
  USBD_LL_FlushEP(pdev, AUDIO_OUT_EP);
  USBD_LL_FlushEP(pdev, AUDIO_IN_EP);


  /* Open EP OUT */
  (void)USBD_LL_CloseEP(pdev, AUDIO_OUT_EP);
  pdev->ep_out[AUDIO_OUT_EP & 0xFU].is_used = 0U;  

  /* Close EP IN */
  USBD_LL_CloseEP(pdev, AUDIO_IN_EP);
  pdev->ep_in[AUDIO_IN_EP & 0xFU].is_used = 0U;


  /* DeInit  physical Interface components */
  if (pdev->pClassDataCmsit[pdev->classId] != NULL)
  {
    ((USBD_AUDIO_ItfTypeDef *)pdev->pUserData[pdev->classId])->DeInit(0U);
    (void)USBD_free(pdev->pClassDataCmsit[pdev->classId]);
    pdev->pClassDataCmsit[pdev->classId] = NULL;
    pdev->pClassData = NULL;
  }

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_AUDIO_Setup
  *         Handle the AUDIO specific requests
  * @param  pdev: instance
  * @param  req: usb requests
  * @retval status
  */
static uint8_t USBD_AUDIO_Setup(USBD_HandleTypeDef *pdev,
                                USBD_SetupReqTypedef *req)
{
  USBD_AUDIO_HandleTypeDef *haudio;
  uint16_t len;
  uint8_t *pbuf;
  uint16_t status_info = 0U;
  USBD_StatusTypeDef ret = USBD_OK;

  haudio = (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (haudio == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
    case USB_REQ_TYPE_CLASS:
      switch (req->bRequest)
      {
        case AUDIO_REQ_GET_CUR:
          AUDIO_REQ_GetCurrent(pdev, req);
          break;

        case AUDIO_REQ_GET_MAX:
          AUDIO_REQ_GetMax(pdev, req);
          break;

        case AUDIO_REQ_GET_MIN:
          AUDIO_REQ_GetMin(pdev, req);
          break;

        case AUDIO_REQ_GET_RES:
          AUDIO_REQ_GetRes(pdev, req);
          break;

        case AUDIO_REQ_SET_CUR:
          AUDIO_REQ_SetCurrent(pdev, req);
          break;

        default:
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;
      }
      break;

    case USB_REQ_TYPE_STANDARD:
      switch (req->bRequest)
      {
        case USB_REQ_GET_STATUS:
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            (void)USBD_CtlSendData(pdev, (uint8_t *)&status_info, 2U);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_GET_DESCRIPTOR:
          if ((req->wValue >> 8) == AUDIO_DESCRIPTOR_TYPE)
          {
            pbuf = (uint8_t *)USBD_AUDIO_GetAudioHeaderDesc(pdev->pConfDesc);
            if (pbuf != NULL)
            {
              len = MIN(USB_AUDIO_DESC_SIZ, req->wLength);
              (void)USBD_CtlSendData(pdev, pbuf, len);
            }
            else
            {
              USBD_CtlError(pdev, req);
              ret = USBD_FAIL;
            }
          }
          break;

        case USB_REQ_GET_INTERFACE:
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            (void)USBD_CtlSendData(pdev, (uint8_t *)&haudio->alt_setting, 1U);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_SET_INTERFACE:
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            if ((uint8_t)(req->wValue) <= USBD_MAX_NUM_INTERFACES)
            {
              if (haudio->alt_setting != (uint8_t)(req->wValue))
              {
                haudio->alt_setting = (uint8_t)(req->wValue);   
                if (haudio->alt_setting == 0U) 
                {
                	AUDIO_OUT_Stop(pdev);
                }
                else 
                {
                	haudio->bit_depth = USBD_AUDIO_BIT_BYTES;
                 	AUDIO_OUT_Restart(pdev);                  
                }
              }
              USBD_LL_FlushEP(pdev, AUDIO_IN_EP);
            }
            else
            {
              /* Call the error management function (command will be NAKed */
              USBD_CtlError(pdev, req);
              ret = USBD_FAIL;
            }
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_CLEAR_FEATURE:
          break;

        default:
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;
      }
      break;
    default:
      USBD_CtlError(pdev, req);
      ret = USBD_FAIL;
      break;
  }

  return (uint8_t)ret;
}

/**
  * @brief  USBD_AUDIO_GetCfgDesc
  *         return configuration descriptor
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_AUDIO_GetCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_AUDIO_CfgDesc);

  return USBD_AUDIO_CfgDesc;
}

static int32_t AUDIO_Get_Vol3dB_Shift(int16_t volume )
{
	if (volume < (int16_t)USBD_AUDIO_VOL_MIN) volume = (int16_t)USBD_AUDIO_VOL_MIN;
	if (volume > (int16_t)USBD_AUDIO_VOL_MAX) volume = (int16_t)USBD_AUDIO_VOL_MAX;
	return (int32_t)((((int16_t)USBD_AUDIO_VOL_MAX - volume) + (int16_t)USBD_AUDIO_VOL_STEP/2)/(int16_t)USBD_AUDIO_VOL_STEP);
}

// ref : https://www.microchip.com/forums/m932509.aspx
static int32_t AUDIO_Volume_Ctrl(int32_t sample, int32_t shift_3dB)
{
	int32_t sample_atten = sample;
	int32_t shift_6dB = shift_3dB>>1;

	if (shift_3dB & 1) 
  {
    // shift_3dB is odd, implement 6dB shift and compensate
    shift_6dB++;
    sample_atten >>= shift_6dB;
    sample_atten += (sample_atten>>1);
  }
	else
  {
    // shift_3dB is even, implement with 6dB shift
    sample_atten >>= shift_6dB;
  }
	return sample_atten;
}

/**
  * @brief  USBD_AUDIO_EP0_RxReady
  *         handle EP0 Rx Ready event
  * @param  pdev: device instance
  * @retval status
  */
static uint8_t USBD_AUDIO_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
  USBD_AUDIO_HandleTypeDef *haudio;
  haudio = (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (haudio == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  if (haudio->control.cmd == AUDIO_REQ_SET_CUR)
  {
    if (haudio->control.req_type == AUDIO_CONTROL_REQ) 
    {
      int16_t volume;

      switch (haudio->control.cs) 
      {
        // Mute Control
        case AUDIO_CONTROL_REQ_FU_MUTE: 
        	haudio->mute = haudio->control.data[0];          
          ((USBD_AUDIO_ItfTypeDef *)pdev->pUserData[pdev->classId])->MuteCtl(haudio->control.data[0]);          
          break;

        // Volume Control
        case AUDIO_CONTROL_REQ_FU_VOL: 
          volume = *(int16_t*)&haudio->control.data[0];
          haudio->volume = volume;
          #if 0
          haudio->vol_3dB_shift = AUDIO_Get_Vol3dB_Shift(volume);
          haudio->volume_percent = AUDIO_Volume_Ctrl(100, haudio->vol_3dB_shift/2);
          #else
          haudio->volume_percent = cmap(volume, (int16_t)USBD_AUDIO_VOL_MIN, (int16_t)USBD_AUDIO_VOL_MAX, 0, 100);
          #endif
          ((USBD_AUDIO_ItfTypeDef*)pdev->pUserData[pdev->classId])->VolumeCtl(haudio->volume_percent);
          break;
      }      
    }

    haudio->control.req_type = 0U;
    haudio->control.cs       = 0U;
    haudio->control.cn       = 0U;
    haudio->control.cmd      = 0U;
    haudio->control.len      = 0U;
  }

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_AUDIO_EP0_TxReady
  *         handle EP0 TRx Ready event
  * @param  pdev: device instance
  * @retval status
  */
static uint8_t USBD_AUDIO_EP0_TxReady(USBD_HandleTypeDef *pdev)
{
  UNUSED(pdev);

  /* Only OUT control data are processed */
  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_AUDIO_SOF
  *         handle SOF event
  * @param  pdev: device instance
  * @retval status
  */
static uint8_t USBD_AUDIO_SOF(USBD_HandleTypeDef *pdev)
{ 
  if (is_init)
  {
    static uint32_t sof_log_cnt = 0;
    sof_log_cnt++;
    if (sof_log_cnt >= 1000)
    {
      sof_log_cnt = 0;
      for (int i=0; i<DATA_RATE_MAX; i++)
      {
        data_in_rate[i] = data_in_count[i];
        data_in_count[i] = 0;
      }
    }    
  }
  return (uint8_t)USBD_OK;
}

void USBD_AUDIO_INFO(void)
{
  static uint32_t pre_time = 0;

  if (millis()-pre_time >= 1000)
  {
    pre_time = millis();
    AUDIO_Log("%d, ISO_IN %3d ISO_OUT %3d IN %3d OUT %-4d FD %d\n", 
      rx_rate/4, 
      data_in_rate[DATA_RATE_ISO_IN_INCOMPLETE],
      data_in_rate[DATA_RATE_ISO_OUT_INCOMPLETE],
      data_in_rate[DATA_RATE_DATA_IN],
      data_in_rate[DATA_RATE_DATA_OUT],
      data_in_rate[DATA_RATE_FEEDBACK]
      );
  }
}

/**
  * @brief  USBD_AUDIO_IsoINIncomplete
  *         handle data ISO IN Incomplete event
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */
static uint8_t USBD_AUDIO_IsoINIncomplete(USBD_HandleTypeDef *pdev, uint8_t epnum)
{

  if (epnum == (AUDIO_IN_EP & 0xF)) 
  {  
    USBD_LL_FlushEP(pdev, AUDIO_IN_EP);
    AUDIO_SendFeedbackFreq(pdev);
  }

  data_in_count[DATA_RATE_ISO_IN_INCOMPLETE]++;
  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_AUDIO_IsoOutIncomplete
  *         handle data ISO OUT Incomplete event
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */
static uint8_t USBD_AUDIO_IsoOutIncomplete(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  USBD_AUDIO_HandleTypeDef *haudio;
  uint16_t packet_length;

  if (pdev->pClassDataCmsit[pdev->classId] == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }
  haudio = (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  USBD_LL_FlushEP(pdev, AUDIO_OUT_EP);

	/* Prepare Out endpoint to receive next audio packet */
  packet_length = (uint16_t)USBD_LL_GetRxDataSize(pdev, epnum);
	(void)USBD_LL_PrepareReceive(pdev, AUDIO_OUT_EP, haudio->buffer, packet_length);

  data_in_count[DATA_RATE_ISO_OUT_INCOMPLETE]++;
  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_AUDIO_DataIn
  *         handle data IN Stage
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */
static uint8_t USBD_AUDIO_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  UNUSED(pdev);

  if (epnum == (AUDIO_IN_EP & 0xF)) 
  {
    AUDIO_UpdateFeedbackFreq(pdev);
    AUDIO_SendFeedbackFreq(pdev);
  }

  data_in_count[DATA_RATE_DATA_IN]++;
  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_AUDIO_DataOut
  *         handle data OUT Stage
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */
static uint8_t USBD_AUDIO_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  USBD_AUDIO_HandleTypeDef *haudio;
  uint16_t packet_length;



  haudio = (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (haudio == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  if (epnum == AUDIO_OUT_EP)
  {
    /* Get received data packet length */
    packet_length = (uint16_t)USBD_LL_GetRxDataSize(pdev, epnum);

    /* Packet received Callback */
    ((USBD_AUDIO_ItfTypeDef *)pdev->pUserData[pdev->classId])->Receive(haudio->buffer, packet_length);
    
    /* Prepare Out endpoint to receive next audio packet */
    USBD_LL_PrepareReceive(pdev,
                            epnum,
                            haudio->buffer,
                            packet_length);    

    rx_count += packet_length;

    static uint32_t pre_time;
    if (millis()-pre_time >= 1000)
    {
      pre_time = millis();
      rx_rate = rx_count;
      rx_count = 0;
    }
  }

  data_in_count[DATA_RATE_DATA_OUT]++;

  return (uint8_t)USBD_OK;
}

/**
  * @brief  DeviceQualifierDescriptor
  *         return Device Qualifier descriptor
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_AUDIO_GetDeviceQualifierDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_AUDIO_DeviceQualifierDesc);

  return USBD_AUDIO_DeviceQualifierDesc;
}

/**
  * @brief  USBD_AUDIO_RegisterInterface
  * @param  pdev: device instance
  * @param  fops: Audio interface callback
  * @retval status
  */
uint8_t USBD_AUDIO_RegisterInterface(USBD_HandleTypeDef *pdev,
                                     USBD_AUDIO_ItfTypeDef *fops)
{
  if (fops == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  pdev->pUserData[pdev->classId] = fops;

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_AUDIO_GetAudioHeaderDesc
  *         This function return the Audio descriptor
  * @param  pdev: device instance
  * @param  pConfDesc:  pointer to Bos descriptor
  * @retval pointer to the Audio AC Header descriptor
  */
static void *USBD_AUDIO_GetAudioHeaderDesc(uint8_t *pConfDesc)
{
  USBD_ConfigDescTypeDef *desc = (USBD_ConfigDescTypeDef *)(void *)pConfDesc;
  USBD_DescHeaderTypeDef *pdesc = (USBD_DescHeaderTypeDef *)(void *)pConfDesc;
  uint8_t *pAudioDesc =  NULL;
  uint16_t ptr;

  if (desc->wTotalLength > desc->bLength)
  {
    ptr = desc->bLength;

    while (ptr < desc->wTotalLength)
    {
      pdesc = USBD_GetNextDesc((uint8_t *)pdesc, &ptr);
      if ((pdesc->bDescriptorType == AUDIO_INTERFACE_DESCRIPTOR_TYPE) &&
          (pdesc->bDescriptorSubType == AUDIO_CONTROL_HEADER))
      {
        pAudioDesc = (uint8_t *)pdesc;
        break;
      }
    }
  }
  return pAudioDesc;
}

/**
  * @brief  AUDIO_Req_GetCurrent
  *         Handles the GET_CUR Audio control request.
  * @param  pdev: device instance
  * @param  req: setup class request
  * @retval status
  */
static void AUDIO_REQ_GetCurrent(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  USBD_AUDIO_HandleTypeDef *haudio;
  haudio = (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (haudio == NULL)
  {
    return;
  }

  if ((req->bmRequest & 0x1f) == AUDIO_CONTROL_REQ) 
  {
    switch (HIBYTE(req->wValue)) 
    {
      case AUDIO_CONTROL_REQ_FU_MUTE: 
        // Current mute state
        USBD_CtlSendData(pdev, &(haudio->mute), 1);
        break;
      case AUDIO_CONTROL_REQ_FU_VOL: 
        // Current volume. See USB Device Class Defintion for Audio Devices v1.0 p.77
        USBD_CtlSendData(pdev, (uint8_t*)&haudio->volume, 2);
        break;
    }
  } 
  else if ((req->bmRequest & 0x1f) == AUDIO_STREAMING_REQ) 
  {
    if (HIBYTE(req->wValue) == AUDIO_STREAMING_REQ_FREQ_CTRL) 
    {
      // Current frequency
      uint32_t freq __attribute__((aligned(4))) = haudio->freq;
      USBD_CtlSendData(pdev, (uint8_t*)&freq, 3);
    }
  }
}

/**
  * @brief  AUDIO_Req_SetCurrent
  *         Handles the SET_CUR Audio control request.
  * @param  pdev: device instance
  * @param  req: setup class request
  * @retval status
  */
static void AUDIO_REQ_SetCurrent(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  USBD_AUDIO_HandleTypeDef *haudio;
  haudio = (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (haudio == NULL)
  {
    return;
  }

  if (req->wLength != 0U)
  {
    /* Prepare the reception of the buffer over EP0 */
    USBD_CtlPrepareRx(pdev,
                      haudio->control.data,
                      req->wLength);

    haudio->control.cmd = AUDIO_REQ_SET_CUR;     /* Set the request value */
    haudio->control.req_type = req->bmRequest & 0x1f; /* Set the request type. See UAC Spec 1.0 - 5.2.1 Request Layout */    
    haudio->control.len = (uint8_t)MIN(req->wLength, USB_MAX_EP0_SIZE);  /* Set the request data length */
    haudio->control.unit = HIBYTE(req->wIndex);  /* Set the request target unit */
    haudio->control.cs = HIBYTE(req->wValue);    /* Set the request control selector (high byte) */
    haudio->control.cn = LOBYTE(req->wValue);    /* Set the request control number (low byte) */
  }
}

/**
 * @brief  AUDIO_Req_GetMax
 *         Handles the GET_MAX Audio control request.
 * @param  pdev: instance
 * @param  req: setup class request
 * @retval status
 */
static void AUDIO_REQ_GetMax(USBD_HandleTypeDef* pdev, USBD_SetupReqTypedef* req)
{
  if ((req->bmRequest & 0x1f) == AUDIO_CONTROL_REQ) {
    switch (HIBYTE(req->wValue)) {
      case AUDIO_CONTROL_REQ_FU_VOL: {
        int16_t vol_max = (int16_t)USBD_AUDIO_VOL_MAX;
        USBD_CtlSendData(pdev, (uint8_t*)&vol_max, 2);
      };
          break;
    }
  }
}

/**
 * @brief  AUDIO_Req_GetMin
 *         Handles the GET_MIN Audio control request.
 * @param  pdev: instance
 * @param  req: setup class request
 * @retval status
 */
static void AUDIO_REQ_GetMin(USBD_HandleTypeDef* pdev, USBD_SetupReqTypedef* req)
{
  if ((req->bmRequest & 0x1f) == AUDIO_CONTROL_REQ) {
    switch (HIBYTE(req->wValue)) {
      case AUDIO_CONTROL_REQ_FU_VOL: {
        int16_t vol_min = (int16_t)USBD_AUDIO_VOL_MIN;
        USBD_CtlSendData(pdev, (uint8_t*)&vol_min, 2);
      };
          break;
    }
  }
}

/**
 * @brief  AUDIO_Req_GetRes
 *         Handles the GET_RES Audio control request.
 * @param  pdev: instance
 * @param  req: setup class request
 * @retval status
 */
static void AUDIO_REQ_GetRes(USBD_HandleTypeDef* pdev, USBD_SetupReqTypedef* req)
{
  if ((req->bmRequest & 0x1f) == AUDIO_CONTROL_REQ) {
    switch (HIBYTE(req->wValue)) {
      case AUDIO_CONTROL_REQ_FU_VOL: {
        int16_t vol_res = (int16_t)USBD_AUDIO_VOL_STEP;
        USBD_CtlSendData(pdev, (uint8_t*)&vol_res, 2);
      };
          break;
    }
  }
}

static  uint32_t AUDIO_GetFeedbackValue(uint32_t rate)
{
  uint32_t freq =  ((rate << 13) + 62) / 125;
  uint32_t ret = 0;
  uint8_t  buf[3];

  buf[0] = freq >> 2;
  buf[1] = freq >> 10;
  buf[2] = freq >> 18;
  
  ret = (buf[2]<<16) | (buf[1]<<8) | (buf[0]<<0);
  
  return ret;
 }

static uint8_t AUDIO_UpdateFeedbackFreq(USBD_HandleTypeDef *pdev)
{
  USBD_AUDIO_HandleTypeDef *haudio;
  haudio = (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId]; 

  uint8_t  buf_level_percent = 50;
  uint32_t fb_gain;


  // 버퍼 사용량 가져오기 
  ((USBD_AUDIO_ItfTypeDef *)pdev->pUserData[pdev->classId])->GetBufferLevel(&buf_level_percent);

  haudio->cur_buf_level = buf_level_percent;

  // 주파수 보정은 100Hz 까지만 한다. 
  // Mac에서 높은 주파수로 보정시 간헐적으로 끊김 현상 발생 
  //
  fb_gain = 100; // Hz


  // 버퍼 사용량을 50%를 목표로 fb_gain 만끔 주파수를 조절한다. 
  //
  if (buf_level_percent > 50)
  {
    haudio->fb_target = AUDIO_GetFeedbackValue(haudio->freq_real + fb_gain);
  }
  else if (buf_level_percent < 50)
  {
    haudio->fb_target = AUDIO_GetFeedbackValue(haudio->freq_real - fb_gain);
  }
  else
  {
    haudio->fb_target = haudio->fb_normal;
  }

  return USBD_OK;
}

static uint8_t AUDIO_SendFeedbackFreq(USBD_HandleTypeDef *pdev)
{
  USBD_AUDIO_HandleTypeDef *haudio;
  haudio = (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId]; 

  USBD_LL_Transmit(pdev, AUDIO_IN_EP, (uint8_t *)&haudio->fb_target, 3);

  data_in_count[DATA_RATE_FEEDBACK]++;
  return USBD_OK;
}

/**
 * @brief  Stop playing and reset buffer pointers
 * @param  pdev: instance
 */
static void AUDIO_OUT_Stop(USBD_HandleTypeDef* pdev)
{
  is_init = false;

  
  AUDIO_Log("AUDIO_OUT_Stop() - IN\n");

  USBD_LL_FlushEP(pdev, AUDIO_IN_EP);
  USBD_LL_FlushEP(pdev, AUDIO_OUT_EP);

  ((USBD_AUDIO_ItfTypeDef *)pdev->pUserData[pdev->classId])->DeInit(0);
  ((USBD_AUDIO_ItfTypeDef *)pdev->pUserData[pdev->classId])->AudioCmd(NULL, 0, AUDIO_CMD_STOP);

  AUDIO_Log("AUDIO_OUT_Stop() - OUT\n");
}

/**
 * @brief  Restart playing with new parameters
 * @param  pdev: instance
 */
static void AUDIO_OUT_Restart(USBD_HandleTypeDef* pdev)
{
  USBD_AUDIO_HandleTypeDef *haudio;
  haudio = (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (haudio == NULL)
  {
    return;
  }

  AUDIO_Log("AUDIO_OUT_Restart() - IN\n");

  USBD_LL_FlushEP(pdev, AUDIO_IN_EP);
  USBD_LL_FlushEP(pdev, AUDIO_OUT_EP);

  
  ((USBD_AUDIO_ItfTypeDef *)pdev->pUserData[pdev->classId])->Init(haudio->freq, haudio->volume_percent, 1);
  ((USBD_AUDIO_ItfTypeDef *)pdev->pUserData[pdev->classId])->AudioCmd(NULL, 0, AUDIO_CMD_START);

  switch (haudio->freq) 
  {
    case 44100:
      haudio->freq_real = 44133;
      haudio->fb_normal = AUDIO_GetFeedbackValue(haudio->freq_real);
      break;
    case 48000:
      haudio->freq_real = 47810;
      haudio->fb_normal = AUDIO_GetFeedbackValue(haudio->freq_real);
      break;
    case 96000:
    default :
      haudio->freq_real = 95621;
      haudio->fb_normal = AUDIO_GetFeedbackValue(haudio->freq_real);
      break;
  }
  haudio->fb_target = haudio->fb_normal;


  AUDIO_Log("AUDIO_OUT_Restart() - OUT\n");
  AUDIO_Log("    freq romal %d 0x%X\n", haudio->freq, haudio->fb_normal);

  /* Prepare Out endpoint to receive 1st packet */
  (void)USBD_LL_PrepareReceive(pdev, AUDIO_OUT_EP, haudio->buffer, AUDIO_OUT_PACKET);

  
  AUDIO_SendFeedbackFreq(pdev);

  is_init = true;
}


void cliCmd(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    if (p_usb_dev != NULL)
    {
      USBD_AUDIO_HandleTypeDef *haudio;
      haudio = (USBD_AUDIO_HandleTypeDef *)p_usb_dev->pClassDataCmsit[p_usb_dev->classId];

      cliShowCursor(false);
      i2sZeroCntClear();
      while(cliKeepLoop())
      {
        int16_t vol_db;

        vol_db = cmap(haudio->volume, (int16_t)USBD_AUDIO_VOL_MIN, (int16_t)USBD_AUDIO_VOL_MAX, -96, 0);

        cliPrintf("freq         : %d KHz\n", haudio->freq/1000);
        cliPrintf("bit          : %d bit\n", haudio->bit_depth * 8);
        cliPrintf("mute         : %s\n", i2sIsMute() ? "True":"False");
        cliPrintf("buf level    : %d %%\n", haudio->cur_buf_level);
        cliPrintf("i2s zero cnt : %d\n", i2sZeroCntGet());
        cliPrintf("vol db       : %d db, 0x%04X\n", vol_db, haudio->volume & 0xFFFF);
        cliPrintf("vol          : %d %%\n", haudio->volume_percent);
        cliPrintf("real rate    : %d Hz\n", rx_rate/(USBD_AUDIO_BIT_BYTES * 2));
        cliPrintf("EP Info\n");
        cliPrintf("   ISO_IN %3d ISO_OUT %3d IN %3d OUT %-4d FD %d\n", 
          data_in_rate[DATA_RATE_ISO_IN_INCOMPLETE],
          data_in_rate[DATA_RATE_ISO_OUT_INCOMPLETE],
          data_in_rate[DATA_RATE_DATA_IN],
          data_in_rate[DATA_RATE_DATA_OUT],
          data_in_rate[DATA_RATE_FEEDBACK]
          );

        cliMoveUp(10);
        delay(50);
      }
      cliMoveDown(10);
      cliShowCursor(true);
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("usb-audio info\n");
  }
}

#include "es8156.h"


#ifdef _USE_HW_ES8156
#include "i2c.h"
#include "cli.h"

#ifdef _USE_HW_RTOS
#define lock()      xSemaphoreTake(mutex_lock, portMAX_DELAY);
#define unLock()    xSemaphoreGive(mutex_lock);
#else
#define lock()
#define unLock()
#endif

static void cliCmd(cli_args_t *args);
static bool es8156InitRegs(void);
static bool readReg(uint8_t reg_addr, uint8_t *p_data);
static bool writeReg(uint8_t reg_addr, uint8_t data);
static bool readRegs(uint8_t reg_addr, uint8_t *p_data, uint32_t length);
static bool writeRegs(uint8_t reg_addr, uint8_t *p_data, uint32_t length);


static uint8_t i2c_ch = _DEF_I2C1;
static uint8_t i2c_addr = 0x08; 
static bool    is_init = false;
static bool    is_detected = false;
#ifdef _USE_HW_RTOS
static SemaphoreHandle_t mutex_lock = NULL;
#endif
static uint8_t main_volume = 75;






bool es8156Init(void)
{
  bool ret;

#ifdef _USE_HW_RTOS
  mutex_lock = xSemaphoreCreateMutex();
#endif

  if (i2cIsBegin(i2c_ch) == true)
    ret = true;
  else
    ret = i2cBegin(i2c_ch, 400);

  if (ret == true && i2cIsDeviceReady(i2c_ch, i2c_addr))
  {    
    is_detected = true;

    es8156InitRegs();
    es8156SetVolume(main_volume);
  }

  is_init = ret;
  logPrintf("[%s] es8156Init()\n", ret ? "OK":"NG");

  cliAdd("es8156", cliCmd);
  return ret;
}


bool es8156InitRegs(void)
{
  bool ret = true;
  uint8_t reg;

  delay(200);

  ret &= writeReg(ES8156_RESET_REG00, 0x1C);
  delay(10);
  ret &= writeReg(ES8156_RESET_REG00, 0x03);


  ret &= writeReg(ES8156_VOLUME_CONTROL_REG14, 100);
  ret &= writeReg(ES8156_ANALOG_SYS2_REG21,    0x07);
  ret &= writeReg(ES8156_CLOCK_ON_OFF_REG08,   0x3F);

  reg  = 0;
  reg |= (1 << 3); // 0 – lineout
                   // 1 – enable HP driver for headphone output
  reg |= (1 << 1); // 0 - SWRMPSEL disable
                   // 1 - SWRMPSEL enable
  reg |= (0 << 0); // 0 - OUT_MUTE normal
                   // 1 - OUT_MUTE mote_output
  ret &= writeReg(ES8156_ANALOG_SYS3_REG22,    reg);

  reg  = 0;
  reg |= (0 << 7); // 0 - enable analog circuits
  reg |= (1 << 6); // 0 - disable reference circuits for output
                   // 1 - enable reference circuits for DAC output
  reg |= (2 << 4); // 0 - VMID power down
                   // 1 - VMID speed charge1
                   // 2 - normal VMID operation
                   // 3 - VMID speed charge3
  reg |= (1 << 3); // 0 – disable HPCOM
                   // 1 - enable HPCOM
  reg |= (0 << 2); // 0 – enable analog DAC reference circuits                   
                   // 1 – power down analog DAC reference circuits
  reg |= (1 << 1); // 0 – disable internal reference circuits
                   // 1 – enable reference circuits
  reg |= (0 << 0); // 0 – enable DAC              
                   // 1 - power down DAC
  ret &= writeReg(ES8156_ANALOG_SYS5_REG25,    reg);

  reg  = 0;
  // reg |= (3<<4);   // 3 - 16-bit
  reg |= (0<<4);   // 0 - 24-bit
  reg |= (0<<0);   // 0 - I2S
                   // 1 - Left Justified 
  ret &= writeReg(ES8156_DAC_SDP_REG11,       reg);

  return ret;
}

bool es8156SetConfig(uint32_t sample_rate, uint32_t sample_depth)
{
  bool ret = true;


  REG02_t reg_02;
  readReg(ES8156_SCLK_MODE_REG02, (uint8_t *)&reg_02);
  if (sample_rate < 48000)
  {
    reg_02.SOFT_MODE_SEL = 0;
    reg_02.SPEED_MODE = 0;
    ret &= writeReg(ES8156_SCLK_MODE_REG02, reg_02.data); 
  }
  else
  {
    reg_02.SOFT_MODE_SEL = 1;
    ret &= writeReg(ES8156_SCLK_MODE_REG02, reg_02.data); 
    reg_02.SPEED_MODE = 1;
    ret &= writeReg(ES8156_SCLK_MODE_REG02, reg_02.data); 
  }
  


  REG11_t reg_11;
  readReg(ES8156_DAC_SDP_REG11, (uint8_t *)&reg_11);
  if (sample_depth == 24)
  {    
    reg_11.SP_WL = 0; 
  }
  else
  {
    reg_11.SP_WL = 3; 
  }
  ret &= writeReg(ES8156_DAC_SDP_REG11, reg_11.data); 
    

   

  return ret;
}

bool es8156SetVolume(uint8_t volume)
{
  bool ret;


  main_volume = constrain(volume, 0, 100); 

  uint8_t d;
  
  d = cmap(main_volume, 0, 100, 0, 255);
  if (0 == main_volume) 
  {
    d = 0;
  }
  ret = writeReg(ES8156_VOLUME_CONTROL_REG14, d);

  return ret;
}

uint8_t es8156GetVolume(void)
{
  return main_volume;
}

bool readReg(uint8_t reg_addr, uint8_t *p_data)
{
  bool ret;

  ret = readRegs(reg_addr, p_data, 1);

  return ret;
}

bool writeReg(uint8_t reg_addr, uint8_t data)
{
  bool ret;

  ret = writeRegs(reg_addr, &data, 1);

  return ret;
}

bool readRegs(uint8_t reg_addr, uint8_t *p_data, uint32_t length)
{
  bool ret;

  lock();
  ret = i2cReadBytes(i2c_ch, i2c_addr, reg_addr, p_data, length, 10);
  unLock();

  return ret;
}

bool writeRegs(uint8_t reg_addr, uint8_t *p_data, uint32_t length)
{
  bool ret;
  uint8_t wr_buf[length + 1];


  lock();
  wr_buf[0] = reg_addr;
  for (int i=0; i<length; i++)
  {
    wr_buf[1+i] = p_data[i];
  }
  ret = i2cWriteData(i2c_ch, i2c_addr, wr_buf, length+1, 50);
  unLock();

  return ret;
}


void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("is_init     : %s\n", is_init ? "True" : "False");
    cliPrintf("is_detected : %s\n", is_detected ? "True" : "False");
    cliPrintf("volume      : %d%%\n", main_volume);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "set_volume"))
  {
    uint8_t volume;

    volume = args->getData(1);

    es8156SetVolume(volume);
    cliPrintf("volume : %d%%\n", main_volume);
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "read"))
  {
    uint8_t addr;
    uint8_t len;
    uint8_t data;

    addr = args->getData(1);
    len  = args->getData(2);

    for (int i=0; i<len; i++)
    {
      if (readRegs(addr + i, &data, 1) == true)
      {
        cliPrintf("0x%02x : 0x%02X\n", addr + i, data);
      }
      else
      {
        cliPrintf("readRegs() Fail\n");
        break;
      }
    }

    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "write"))
  {
    uint8_t addr;
    uint8_t data;

    addr = args->getData(1);
    data = args->getData(2);


    if (writeRegs(addr, &data, 1) == true)
    {
      cliPrintf("0x%02x : 0x%02X\n", addr, data);
    }
    else
    {
      cliPrintf("writeRegs() Fail\n");
    }

    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("es8156 info\n");
    cliPrintf("es8156 set_volume 0~100\n");
    cliPrintf("es8156 read addr[0~0xFF] len[0~255]\n");
    cliPrintf("es8156 write addr[0~0xFF] data \n");    
  }
}

#endif
#ifndef PCA9554_H_
#define PCA9554_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"


#ifdef _USE_HW_PCA9554


typedef enum
{
  PCA9554_REG_INPUT    = 0,
  PCA9554_REG_OUTPUT   = 1,
  PCA9554_REG_POLARITY = 2,
  PCA9554_REG_CONFIG   = 3,
  PCA9554_REG_MAX,
} PCA9554Reg_t;

typedef enum
{
  PCA9554_MODE_INPUT   = _DEF_INPUT,
  PCA9554_MODE_OUTPUT  = _DEF_OUTPUT,
} PCA9554Mode_t;

bool pca9554Init(void);
bool pca9554IsInit(void);

bool pca9554PinGetMode(uint8_t pin_num, PCA9554Mode_t *p_mode);
bool pca9554PinSetMode(uint8_t pin_num, PCA9554Mode_t mode);
bool pca9554PinRead(uint8_t pin_num, uint8_t *p_data);
bool pca9554PinWrite(uint8_t pin_num, uint8_t data);

bool pca9554RegRead(PCA9554Reg_t reg, uint8_t *p_data);
bool pca9554RegWrite(PCA9554Reg_t reg, uint8_t data);


#endif


#ifdef __cplusplus
}
#endif

#endif
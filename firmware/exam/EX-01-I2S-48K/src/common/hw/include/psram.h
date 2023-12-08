#ifndef PSRAM_H_
#define PSRAM_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"

#ifdef _USE_HW_PSRAM


bool psramInit(void);
bool psramIsInit(void);

bool psramRead(uint32_t addr, uint8_t *p_data, uint32_t length);
bool psramWrite(uint32_t addr, uint8_t *p_data, uint32_t length);
uint32_t psramGetAddr(void);
uint32_t psramGetLength(void);


#endif


#ifdef __cplusplus
}
#endif

#endif 
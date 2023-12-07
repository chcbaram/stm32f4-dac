#ifndef POWER_H_
#define POWER_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"

#ifdef _USE_HW_POWER


bool powerInit(void);
bool powerUpdate(void);


#endif

#ifdef __cplusplus
}
#endif

#endif 

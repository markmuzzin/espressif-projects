#ifndef __PWM_H__
#define __PWM_H__

/**********************************************/
/*                TYPEDEFS                    */
/**********************************************/
typedef enum _ePwmDevice_t 
{
    ePwmEngine = 0,
    ePwmFrontLight,
    ePwmBackLight,
    ePwmMaxDevices
} ePwmDevice_t;

typedef enum _ePwmReturn_t 
{
    ePwmInvalidDevice = -3,
    ePwmInvalidParam,
    ePwmFail,
    ePwmSuccess,
} ePwmReturn_t;

/**********************************************/
/*                 APIS                       */
/**********************************************/
ePwmReturn_t PwmInitialize(TaskHandle_t *taskNotifyHandle);
ePwmReturn_t PwmSetValue(ePwmDevice_t device, uint32_t duty, uint8_t immediate);
uint8_t PwmGetCurrentPwm(ePwmDevice_t device);

#endif

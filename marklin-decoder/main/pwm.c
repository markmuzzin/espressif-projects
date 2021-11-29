#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "pwm.h"
#include "common.h"


/* Uncomment to enable prints for PWM setting algo */
//#define ENABLE_DEBUG 1

/* PWM channel common setup */
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES           LEDC_TIMER_8_BIT
#define LEDC_FREQUENCY          (60)
#define PWM_DELTA_DELAY_MS      (400)


typedef struct _pwmConfiguration_t {
    ledc_channel_config_t ledc_channel;
    uint32_t channel;
    uint32_t gpioNumber;
    uint32_t maxDutyCycle;
    uint32_t stepRate;
    uint32_t targetDutyCycle;
    uint32_t currentDutyCycle;
    uint8_t immediate;
} pwmConfiguration_t;


#ifdef ENABLE_DEBUG
static const char ePwmDeviceString[][25] = 
{
    "ePwmMotor",
    "ePwmFrontLight",
    "ePwmBackLight",
};
#endif


static SemaphoreHandle_t pwmMutex;

static pwmConfiguration_t pwmConfig[ePwmMaxDevices] = 
{
    { {0}, LEDC_CHANNEL_0, PWM_MOTOR, 100, 5, 0, 0, 0 }, /* ePwmMotor */
    { {0}, LEDC_CHANNEL_1, PWM_LIGHT_FRONT, 100, 10, 0, 0, 0 }, /* ePwmFrontLight */
    { {0}, LEDC_CHANNEL_2, PWM_LIGHT_REAR, 100, 10, 0, 0, 0 }, /* ePwmBackLight */
};


static void pwmProcessingTask(void *arg)
{
    int32_t device;
    uint32_t maxDutyCycleCount;

    while(1)
    {
        /* Lock PWM setting */
        xSemaphoreTake(pwmMutex, portMAX_DELAY);

        for(device=0; device<ePwmMaxDevices; device++)
        {
            /* Convert duty cycle to the duty cycle count */
            maxDutyCycleCount = (uint32_t)( (pow(2,LEDC_DUTY_RES) - 1) * (pwmConfig[device].maxDutyCycle/100.0));

            /* Check if the setting needs to be immediate */
            if(pwmConfig[device].immediate == 1)
            {
                /* Clear immediate flag */
                pwmConfig[device].immediate = 0;

                /* Set target duty cycle to current */
                pwmConfig[device].currentDutyCycle = pwmConfig[device].targetDutyCycle;
#if ENABLE_DEBUG
                printf("Setting PWM: %s target: %d current %d\n", ePwmDeviceString[device], pwmConfig[device].targetDutyCycle, pwmConfig[device].currentDutyCycle);
#endif
                /* Set and update duty cycle */
                ledc_set_duty(pwmConfig[device].ledc_channel.speed_mode, 
                    pwmConfig[device].ledc_channel.channel, pwmConfig[device].currentDutyCycle);
                ledc_update_duty(pwmConfig[device].ledc_channel.speed_mode, 
                    pwmConfig[device].ledc_channel.channel);

            }

            /* If the target duty cycle is greater than the current duty cycle */
            else if(pwmConfig[device].targetDutyCycle > pwmConfig[device].currentDutyCycle)
            {

                /* Check that we don't set a value over the max duty cycle rate */
                if( pwmConfig[device].currentDutyCycle + pwmConfig[device].stepRate > maxDutyCycleCount )
                {
                    /* Set the current duty cycle to the maximum value */
                    pwmConfig[device].currentDutyCycle = maxDutyCycleCount;
                }
                else
                {
                    /* If the target duty cycle plus the step rate is greater than the current duty cycle */
                    if( pwmConfig[device].currentDutyCycle + pwmConfig[device].stepRate > pwmConfig[device].targetDutyCycle )
                    {
                        /* Set the current duty cycle to the target duty cycle */
                        pwmConfig[device].currentDutyCycle = pwmConfig[device].targetDutyCycle;
                    }
                    else
                    {
                        /* Set the current duty cycle plus the step rate */
                        pwmConfig[device].currentDutyCycle += pwmConfig[device].stepRate;
                    }
                }
#if ENABLE_DEBUG
                printf("Raising PWM: %s target: %d current %d\n", ePwmDeviceString[device], pwmConfig[device].targetDutyCycle, pwmConfig[device].currentDutyCycle);
#endif
                /* Set and update duty cycle */
                ledc_set_duty(pwmConfig[device].ledc_channel.speed_mode, 
                    pwmConfig[device].ledc_channel.channel, pwmConfig[device].currentDutyCycle);
                ledc_update_duty(pwmConfig[device].ledc_channel.speed_mode, 
                    pwmConfig[device].ledc_channel.channel);
            } 

            /* If the target duty cycle is less than the current duty cycle */
            else if(pwmConfig[device].targetDutyCycle < pwmConfig[device].currentDutyCycle)
            {

                /* Check that we don't underflow */
                if( pwmConfig[device].currentDutyCycle < pwmConfig[device].stepRate )
                {
                    /* Set the current duty cycle to zero */
                    pwmConfig[device].currentDutyCycle = 0;
                }
                else
                {
                    /* If the target duty cycle less the step rate is less than the current duty cycle */
                    if( pwmConfig[device].currentDutyCycle - pwmConfig[device].stepRate < pwmConfig[device].targetDutyCycle )
                    {
                        /* Set the current duty cycle to the target duty cycle */
                        pwmConfig[device].currentDutyCycle = pwmConfig[device].targetDutyCycle;
                    }
                    else
                    {
                        /* Set the current duty cycle less the step rate */
                        pwmConfig[device].currentDutyCycle -= pwmConfig[device].stepRate;
                    }

                }
#if ENABLE_DEBUG
                printf("Lowering PWM: %s target: %d current %d\n", ePwmDeviceString[device], pwmConfig[device].targetDutyCycle, pwmConfig[device].currentDutyCycle);
#endif
                /* Set and update duty cycle */
                ledc_set_duty(pwmConfig[device].ledc_channel.speed_mode, 
                    pwmConfig[device].ledc_channel.channel, pwmConfig[device].currentDutyCycle);
                ledc_update_duty(pwmConfig[device].ledc_channel.speed_mode, 
                    pwmConfig[device].ledc_channel.channel);
            }
            else 
            {
#if ENABLE_DEBUG
                printf("Stack high watermark: %d\n", uxTaskGetStackHighWaterMark(NULL));
#endif
                /* Do nothing - target has been met for this device */
            }
         }

        /* Unlock PWM setting */
        xSemaphoreGive(pwmMutex);

        /* Step interval */
        vTaskDelay(pdMS_TO_TICKS(PWM_DELTA_DELAY_MS));
    }   
 }

ePwmReturn_t PwmInitialize(void)
{
    uint32_t device;
    ePwmReturn_t ret = ePwmSuccess;

    /* Create mutex */
    pwmMutex = xSemaphoreCreateMutex();

    /* Initialize timer/pwm */
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
    };

    if ( ESP_OK != ledc_timer_config(&ledc_timer))
    {
        printf("[%s] Error initializing timer config\n", __func__);
        ret = ePwmFail;    
    }

    if ( ePwmSuccess == ret )
    {
        /* Initialize PWM devices */
        for(device=0; device<ePwmMaxDevices && (ePwmSuccess == ret); device++)
        {
            pwmConfig[device].ledc_channel.speed_mode     = LEDC_MODE;
            pwmConfig[device].ledc_channel.channel        = pwmConfig[device].channel;
            pwmConfig[device].ledc_channel.timer_sel      = LEDC_TIMER;
            pwmConfig[device].ledc_channel.intr_type      = LEDC_INTR_DISABLE;
            pwmConfig[device].ledc_channel.gpio_num       = pwmConfig[device].gpioNumber;
            pwmConfig[device].ledc_channel.duty           = 0;
            pwmConfig[device].ledc_channel.hpoint         = 0;
        
            /* Initialize the channel config */
            if ( ESP_OK != ledc_channel_config(&pwmConfig[device].ledc_channel))
            {
                printf("[%s] Error initializing channel config for GPIO %d\n", __func__, pwmConfig[device].gpioNumber);
                ret = ePwmFail;            
            }
        }
    }

    if ( ePwmSuccess == ret )
    {
        /* Start the PWM thread */
        if( pdPASS != xTaskCreate(pwmProcessingTask, "pwm_processing_task", 8192, NULL, 4, NULL))
        {
            printf("[%s] Error starting PWM thread\n", __func__);
            ret = ePwmFail;
        }
    }

    return ret;
}


ePwmReturn_t PwmSetValue(ePwmDevice_t device, uint32_t dutyCycle, uint8_t immediate)
{
    ePwmReturn_t ret = ePwmSuccess;

    /* If the device is invalid */
    if( device >= ePwmMaxDevices )
    {
        ret = ePwmInvalidDevice;
    }    
    /* If the duty cycle is invalid */
    else if( dutyCycle > 100)
    {
        ret = ePwmInvalidParam;
    }
    else
    {
        /* Set target duty cycle */
        xSemaphoreTake(pwmMutex, portMAX_DELAY);
        pwmConfig[device].immediate = immediate;
        pwmConfig[device].targetDutyCycle = (uint32_t)((pow(2,LEDC_DUTY_RES) - 1) * ((pwmConfig[device].maxDutyCycle/100.0) * dutyCycle) / 100.0);
        xSemaphoreGive(pwmMutex);
    }
    
    return ret;
}







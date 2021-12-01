#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "pwm.h"
#include "common.h"

/**********************************************/
/*                DEFINES                     */
/**********************************************/
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES           LEDC_TIMER_8_BIT
#define LEDC_FREQUENCY          (60)
#define PWM_DELTA_DELAY_MS      (400)


/**********************************************/
/*                TYPEDEFS                    */
/**********************************************/
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


/**********************************************/
/*              VARIABLES                     */
/**********************************************/
static const char *TAG = "LOCO-PWM";

static SemaphoreHandle_t gPwmMutex;

static pwmConfiguration_t gPwmConfig[ePwmMaxDevices] = 
{
    { {0}, LEDC_CHANNEL_0, PWM_MOTOR, 100, 5, 0, 0, 0 },        /* ePwmMotor */
    { {0}, LEDC_CHANNEL_1, PWM_LIGHT_FRONT, 100, 10, 0, 0, 0 }, /* ePwmFrontLight */
    { {0}, LEDC_CHANNEL_2, PWM_LIGHT_REAR, 100, 10, 0, 0, 0 },  /* ePwmBackLight */
};

static const char gPwmDeviceString[][25] = 
{
    "ePwmMotor",
    "ePwmFrontLight",
    "ePwmBackLight",
};


/**********************************************/
/*              FUNCTIONS                     */
/**********************************************/
static void S_PwmProcessingTask(void *arg)
{
    int32_t device = 0;
    uint32_t maxDutyCycleCount = 0;

    while(1)
    {
        /* Lock PWM setting */
        xSemaphoreTake(gPwmMutex, portMAX_DELAY);

        for(device=0; device<ePwmMaxDevices; device++)
        {
            /* Convert duty cycle to the duty cycle count */
            maxDutyCycleCount = (uint32_t)( (pow(2,LEDC_DUTY_RES) - 1) * (gPwmConfig[device].maxDutyCycle/100.0));

            /* Check if the setting needs to be immediate */
            if(gPwmConfig[device].immediate == 1)
            {
                /* Clear immediate flag */
                gPwmConfig[device].immediate = 0;

                /* Set target duty cycle to current */
                gPwmConfig[device].currentDutyCycle = gPwmConfig[device].targetDutyCycle;

                ESP_LOGI(TAG,"Setting PWM: %s target: %d current %d\n", gPwmDeviceString[device], gPwmConfig[device].targetDutyCycle, gPwmConfig[device].currentDutyCycle);

                /* Set and update duty cycle */
                ledc_set_duty(gPwmConfig[device].ledc_channel.speed_mode, 
                    gPwmConfig[device].ledc_channel.channel, gPwmConfig[device].currentDutyCycle);
                ledc_update_duty(gPwmConfig[device].ledc_channel.speed_mode, 
                    gPwmConfig[device].ledc_channel.channel);

            }

            /* If the target duty cycle is greater than the current duty cycle */
            else if(gPwmConfig[device].targetDutyCycle > gPwmConfig[device].currentDutyCycle)
            {

                /* Check that we don't set a value over the max duty cycle rate */
                if( gPwmConfig[device].currentDutyCycle + gPwmConfig[device].stepRate > maxDutyCycleCount )
                {
                    /* Set the current duty cycle to the maximum value */
                    gPwmConfig[device].currentDutyCycle = maxDutyCycleCount;
                }
                else
                {
                    /* If the target duty cycle plus the step rate is greater than the current duty cycle */
                    if( gPwmConfig[device].currentDutyCycle + gPwmConfig[device].stepRate > gPwmConfig[device].targetDutyCycle )
                    {
                        /* Set the current duty cycle to the target duty cycle */
                        gPwmConfig[device].currentDutyCycle = gPwmConfig[device].targetDutyCycle;
                    }
                    else
                    {
                        /* Set the current duty cycle plus the step rate */
                        gPwmConfig[device].currentDutyCycle += gPwmConfig[device].stepRate;
                    }
                }
                ESP_LOGI(TAG,"Raising PWM: %s target: %d current %d\n", gPwmDeviceString[device], gPwmConfig[device].targetDutyCycle, gPwmConfig[device].currentDutyCycle);

                /* Set and update duty cycle */
                ledc_set_duty(gPwmConfig[device].ledc_channel.speed_mode, 
                    gPwmConfig[device].ledc_channel.channel, gPwmConfig[device].currentDutyCycle);
                ledc_update_duty(gPwmConfig[device].ledc_channel.speed_mode, 
                    gPwmConfig[device].ledc_channel.channel);
            } 

            /* If the target duty cycle is less than the current duty cycle */
            else if(gPwmConfig[device].targetDutyCycle < gPwmConfig[device].currentDutyCycle)
            {

                /* Check that we don't underflow */
                if( gPwmConfig[device].currentDutyCycle < gPwmConfig[device].stepRate )
                {
                    /* Set the current duty cycle to zero */
                    gPwmConfig[device].currentDutyCycle = 0;
                }
                else
                {
                    /* If the target duty cycle less the step rate is less than the current duty cycle */
                    if( gPwmConfig[device].currentDutyCycle - gPwmConfig[device].stepRate < gPwmConfig[device].targetDutyCycle )
                    {
                        /* Set the current duty cycle to the target duty cycle */
                        gPwmConfig[device].currentDutyCycle = gPwmConfig[device].targetDutyCycle;
                    }
                    else
                    {
                        /* Set the current duty cycle less the step rate */
                        gPwmConfig[device].currentDutyCycle -= gPwmConfig[device].stepRate;
                    }

                }
                ESP_LOGI(TAG,"Lowering PWM: %s target: %d current %d\n", gPwmDeviceString[device], gPwmConfig[device].targetDutyCycle, gPwmConfig[device].currentDutyCycle);

                /* Set and update duty cycle */
                ledc_set_duty(gPwmConfig[device].ledc_channel.speed_mode, 
                    gPwmConfig[device].ledc_channel.channel, gPwmConfig[device].currentDutyCycle);
                ledc_update_duty(gPwmConfig[device].ledc_channel.speed_mode, 
                    gPwmConfig[device].ledc_channel.channel);
            }
            else 
            {
                //ESP_LOGI(TAG,"Stack high watermark: %d\n", uxTaskGetStackHighWaterMark(NULL));
                /* Do nothing - target has been met for this device */
            }
         }

        /* Unlock PWM setting */
        xSemaphoreGive(gPwmMutex);

        /* Step interval */
        vTaskDelay(pdMS_TO_TICKS(PWM_DELTA_DELAY_MS));
    }   
 }

ePwmReturn_t PwmInitialize(void)
{
    uint32_t device;
    ePwmReturn_t ret = ePwmSuccess;

    /* Create mutex */
    gPwmMutex = xSemaphoreCreateMutex();

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
            gPwmConfig[device].ledc_channel.speed_mode     = LEDC_MODE;
            gPwmConfig[device].ledc_channel.channel        = gPwmConfig[device].channel;
            gPwmConfig[device].ledc_channel.timer_sel      = LEDC_TIMER;
            gPwmConfig[device].ledc_channel.intr_type      = LEDC_INTR_DISABLE;
            gPwmConfig[device].ledc_channel.gpio_num       = gPwmConfig[device].gpioNumber;
            gPwmConfig[device].ledc_channel.duty           = 0;
            gPwmConfig[device].ledc_channel.hpoint         = 0;
        
            /* Initialize the channel config */
            if ( ESP_OK != ledc_channel_config(&gPwmConfig[device].ledc_channel))
            {
                printf("[%s] Error initializing channel config for GPIO %d\n", __func__, gPwmConfig[device].gpioNumber);
                ret = ePwmFail;            
            }
        }
    }

    if ( ePwmSuccess == ret )
    {
        /* Start the PWM thread */
        if( pdPASS != xTaskCreate(S_PwmProcessingTask, "pwm_processing_task", 8192, NULL, 4, NULL))
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
        xSemaphoreTake(gPwmMutex, portMAX_DELAY);
        gPwmConfig[device].immediate = immediate;
        gPwmConfig[device].targetDutyCycle = (uint32_t)((pow(2,LEDC_DUTY_RES) - 1) * ((gPwmConfig[device].maxDutyCycle/100.0) * dutyCycle) / 100.0);
        xSemaphoreGive(gPwmMutex);
    }
    
    return ret;
}







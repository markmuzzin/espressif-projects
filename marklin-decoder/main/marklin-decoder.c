#include <stdio.h>
#include "esp_spi_flash.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include <string.h>
#include "pwm.h"
#include "dac.h"
#include "esp_err.h"
#include "esp_log.h"
#include "sounds.h"


/* TODO List */
/* - Mixing a second sound */


static const char *TAG = "LOCO-MAIN";


typedef struct _EngineSound_t
{
    const uint8_t *wave;
    uint32_t size;
    uint8_t minDuty;
    uint8_t maxDuty;   
} EngineSound_t;


static EngineSound_t sounds[] =
{
    {NULL,          0, 0, 49},
    {trainchug1,    sizeof(trainchug1), 50, 59},
    {trainchug2,    sizeof(trainchug2), 60, 69},
    {trainchug3,    sizeof(trainchug3), 70, 79},
    {trainchug4,    sizeof(trainchug4), 80, 89},
    {trainchug5,    sizeof(trainchug5), 90, 99},
    {trainchug6,    sizeof(trainchug6), 100, 101},
};


static TaskHandle_t gEngineSoundTaskHandle = NULL;


static void S_MarklinSetEngineSoundTask(void *arg)
{
    uint32_t dutyCycle = 0;
    uint32_t i;

    while(1)
    {
        /* Wait for PWM task notification */
        ESP_LOGI(TAG,"[%s] Waiting on task notification", __func__);

        xTaskNotifyWaitIndexed(0, 0x00, ULONG_MAX, &dutyCycle, portMAX_DELAY);

        ESP_LOGI(TAG,"[%s] Engine cycle change: %d", __func__, dutyCycle);

        for(i=0; i<sizeof(sounds)/sizeof(sounds[0]); i++)
        {
            if(dutyCycle >= sounds[i].minDuty && dutyCycle < sounds[i].maxDuty)
            {
                if(sounds[i].wave != NULL)
                {
                    ESP_LOGI(TAG,"[%s] Selected sound: %d size %d", __func__, i, sounds[i].size);
                    DacBreakRepeatPlayback();
                    DacPlayWaveData("CHUG", sounds[i].wave + sizeof(WaveFileHeader_t), sounds[i].size - sizeof(WaveFileHeader_t), INF_REP);
                }
                else
                {
                    ESP_LOGI(TAG,"[%s] Stop sound playback", __func__);
                    DacStopWavePlayback();   
                }
                break;
            }
        }
    }
}


static void MarklinDecoderInitialize(void)
{
    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    ESP_LOGI(TAG,"[%s] Marklin-Decoder ESP-NOW with %d CPU cores", __func__, chip_info.cores);

    /* Start the PWM thread */
    if( pdPASS == xTaskCreate(S_MarklinSetEngineSoundTask, "S_MarklinSetEngineSoundTask", 8192, NULL, 4, &gEngineSoundTaskHandle))
    {
        /* Initialize PWM - with engine sound */
//        PwmInitialize(gEngineSoundTaskHandle);

        /* Initialize PWM - with no engine sound */
        PwmInitialize(NULL);

        /* Initialize DAC */
        DacInitialize();
    }
    else
    {
        /* Error starting PWM task */    
        ESP_LOGE(TAG,"[%s] Error starting engine sound task", __func__);
    }
}

/* Test App */

/* Temp for testing */
uint32_t dutyValuesEngine[]     = {50, 60, 70, 80, 90};
uint32_t dutyValuesFrontLight[] = {0, 35, 65, 75, 85};
uint32_t dutyValuesBackLight[]  = {0, 45, 66, 99, 45};

void app_main()
{
    uint32_t j = 0;

    /* Initialize modules */
    MarklinDecoderInitialize();


    while(1)
    {
printf("setting %d %d\n", j, dutyValuesEngine[j]);
        if (j==0)
            PwmSetValue(ePwmEngine, dutyValuesEngine[j], 1);
        else
            PwmSetValue(ePwmEngine, dutyValuesEngine[j], 0);
     
//        MarklinSetEngineSound(dutyValuesEngine[j]);
        sleep(10);


        j++;
        if (j == sizeof(dutyValuesEngine)/sizeof(dutyValuesEngine[0]))
        {
            j=0;
        }
    } 

    fflush(stdout);
    esp_restart();
}



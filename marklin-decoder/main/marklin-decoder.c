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


static const char *TAG = "LOCO-MAIN";


static void MarklinDecoderInitialize(void)
{
    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("Marklin-Decoder ESP-NOW with %d CPU cores\n, ", chip_info.cores);

    /* Initialize PWM */
    PwmInitialize();
    /* Initialize DAC */
    DacInitialize();
    /* Set WAV parameters */    
    //DacSetWaveParameters(trainchug1);
}


void app_main()
{
    uint32_t j = 0;

    /* Initialize modules */
    MarklinDecoderInitialize();

    uint32_t dutyValues[5] = {0, 35, 65, 75, 85};
    uint32_t dutyValues2[5] = {0, 45, 66, 99, 45};
    uint32_t dutyValues3[] = {0, 16, 24, 40, 55, 62, 74, 90};

    while(1)
    {
        //MarklinDecoderSetSpeed(ePwmMotor, dutyValues3[j]);
   
        DacBreakRepeatPlayback();
        DacPlayWaveData("CHUG", trainchug1 + 44, sizeof(trainchug1)-45, INF_REP);
        sleep(10);
        DacPlayWaveData("CHUG", trainchug2 + 44, sizeof(trainchug2)-45, 3);        
        DacBreakRepeatPlayback();
        DacPlayWaveData("CHUG", trainchug3 + 44, sizeof(trainchug3)-45, 3);
        DacPlayWaveData("CHUG", trainchug4 + 44, sizeof(trainchug4)-45, 3);
        DacPlayWaveData("CHUG", trainchug5 + 44, sizeof(trainchug5)-45, 3);
        DacPlayWaveData("CHUG", trainchug6 + 44, sizeof(trainchug6)-45, 3);
        DacPlayWaveData("CHUG", trainchug7 + 44, sizeof(trainchug7)-45, 3);
        DacPlayWaveData("CHUG", trainchug8 + 44, sizeof(trainchug8)-45, 3);
        DacPlayWaveData("CHUG", trainchug9 + 44, sizeof(trainchug9)-45, 3);
        DacPlayWaveData("CHUG", trainchug10 + 44, sizeof(trainchug10)-45, 3);
        DacPlayWaveData("CHUG", trainchug11 + 44, sizeof(trainchug11)-45, 3);
        DacPlayWaveData("CHUG", trainchug12 + 44, sizeof(trainchug12)-45, 3);
        DacPlayWaveData("CHUG", trainchug13 + 44, sizeof(trainchug13)-45, 3);
        DacPlayWaveData("CHUG", trainchug14 + 44, sizeof(trainchug14)-45, 3);
        DacPlayWaveData("CHUG", trainchug15 + 44, sizeof(trainchug15)-45, 3);

        sleep(60);
        j++;

        if (j>8)
            j=0;
    } 

    fflush(stdout);
    esp_restart();
}



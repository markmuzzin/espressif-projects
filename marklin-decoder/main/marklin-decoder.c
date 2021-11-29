#include <stdio.h>
#include "esp_spi_flash.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include <string.h>
#include "pwm.h"
#include "dac.h"




#include "audio_example_file.h"


#define ENABLE_PWM 1
#define ENABLE_WAVE 1


static void MarklinDecoderInitialize(void)
{
    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("Marklin-Decoder ESP-NOW with %d CPU cores\n, ", chip_info.cores);

#ifdef ENABLE_PWM
    /* Initialize PWM */
    PwmInitialize();
#endif
}



void app_main()
{
    uint32_t j = 0;

    /* Initialize modules */
    MarklinDecoderInitialize();

    //example_i2s_init();
    //Odroid_InitializeAudio();

    uint32_t dutyValues[5] = {45, 55, 65, 75, 85};
    uint32_t dutyValues2[5] = {33, 45, 66, 99, 45};
    uint32_t dutyValues3[5] = {55, 45, 24, 88, 2};

    //dac_output_enable(DAC_CHANNEL_1);
    //dac_output_voltage(DAC_CHANNEL_1, 200);

   //PwmSetValue(ePwmMotor, dutyValues[2], 1);



    while(1)
    {
#ifdef ENABLE_PWM
        PwmSetValue(ePwmMotor, dutyValues[j], 0);
        PwmSetValue(ePwmFrontLight, dutyValues2[j], 0);
        PwmSetValue(ePwmBackLight, dutyValues3[j], 0);
#endif

#ifdef ENABLE_WAVE
        DacPlayWaveForm(pcm1608s, sizeof(pcm1608s));
#endif
        sleep(8);

        j++;

        if (j>4)
            j=0;
    } 

    fflush(stdout);
    esp_restart();
}

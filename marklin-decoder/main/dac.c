#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2s.h"
#include "dac.h"

/**********************************************/
/*                DEFINES                     */
/**********************************************/
#define I2S_DATA_MULTIPLIER               2
#define I2S_MAX_BYTES_PER_DMA_WRITE       64
#define I2S_MAX_ACTUAL_BYTES_PER_WRITE    (I2S_MAX_BYTES_PER_DMA_WRITE/I2S_DATA_MULTIPLIER)
#define I2S_MAX_BUFFER_COUNT              32
#define I2S_MAX_TASK_QUEUE                25
#define I2S_DEFAULT_SAMPLE_RATE           8000
#define I2S_DEFAULT_SAMPLE_BITS           16


/**********************************************/
/*                TYPEDEFS                    */
/**********************************************/
typedef struct _QueueData_t_t 
{
    char name[10];
    uint8_t repeat;
    const uint8_t *buffer;
    uint32_t length;
} QueueData_t;


/**********************************************/
/*              VARIABLES                     */
/**********************************************/
static const char *TAG = "LOCO-DAC";

static QueueHandle_t gI2sPlayTaskQueue = NULL;
static QueueHandle_t gI2sEventQueue = NULL;

static bool gStopWavePlayback = false;
static bool gBreakRepeatPlayback = false;


/**********************************************/
/*              FUNCTIONS                     */
/**********************************************/
static void S_DacDumpWaveHeaderData(WaveFileHeader_t *header)
{
    ESP_LOGI(TAG,"\tgroupId: %.4s", header->groupId);
    ESP_LOGI(TAG,"\ttotalFileLength: %d", header->totalFileLength);
    ESP_LOGI(TAG,"\twave: %.4s", header->wave);
    ESP_LOGI(TAG,"\tformatChunk: %.4s", header->formatChunk);
    ESP_LOGI(TAG,"\tlengthOfFormatData: %d", header->lengthOfFormatData);
    ESP_LOGI(TAG,"\ttypeOfFormat: %d - %s", header->typeOfFormat, (header->typeOfFormat == 1) ? "PCM":"I2S");
    ESP_LOGI(TAG,"\tnumChannels: %d", header->numChannels);
    ESP_LOGI(TAG,"\tsampleRate: %d", header->sampleRate);
    ESP_LOGI(TAG,"\tbytesPerSecond: %d", header->bytesPerSecond);
    ESP_LOGI(TAG,"\taudioFormat: %d - %s", header->audioFormat, (header->audioFormat == 1) ? "8-bit Mono": (header->audioFormat == 2) ? "8 bit stereo/16 bit mono" : "16 bit stereo");
    ESP_LOGI(TAG,"\tbitsPerSample: %d", header->bitsPerSample);
    ESP_LOGI(TAG,"\tdataChunk: %.4s", header->dataChunk);
    ESP_LOGI(TAG,"\tdataSize: %d", header->dataSize);
}


static uint32_t S_DacScaleAndFormatWaveData(uint8_t* d_buff, uint8_t* s_buff, uint32_t len)
{
    uint32_t j = 0;

    for (uint32_t i = 0; i < len; i+=4) 
    {
        /* Add padding due to 8-bit internal DAC vs. 32bit DMA */
        for(uint32_t m=0; m<I2S_DATA_MULTIPLIER; m++)
        {
            /* MSB bits for sample values to 8-bit DAC, scaled by 0x80 */
            d_buff[j++] = 0x80;
            d_buff[j++] = s_buff[i+1] + 0x80;
            d_buff[j++] = 0x80;
            d_buff[j++] = s_buff[i+3] + 0x80;
       }
    }

    return (len * I2S_DATA_MULTIPLIER);
}


static void S_DacPlayTask(void *arg)
{
    i2s_event_t i2s_evt;
    uint32_t bytesWritten = 0;
    uint32_t bytesToWrite = 0;
    uint32_t i2cWriteLength = 0;
    bool waveStartPlayback = false;
    bool wavePlaying = false;
    bool clearTxBuffer = false;
    QueueData_t waveData = {0};

    uint32_t totalBytesWritten = 0;
    uint8_t *i2s_write_buff = (uint8_t *) malloc(4096*6);

    while(1)
    {
        /* Check if there is no WAV already playing */
        if (wavePlaying == false)
        {
            /* Check if there is a WAV to play in the queue*/
            if (xQueueReceive(gI2sPlayTaskQueue, &waveData, 1))
            {
                /* Reset break playback flag */
                gBreakRepeatPlayback = false;
                /* Set wave start playback flag to true */
                waveStartPlayback = true;
                /* Set wave playing flag to true */
                wavePlaying = true;
                /* Set total bytes written to 0 */
                totalBytesWritten = 0;
                ESP_LOGI(TAG, "[%s] Start playback", waveData.name);
            }
            else
            {
                /* If there are no more WAVs to play */
                if(clearTxBuffer == true)
                {
                    /* Set flag to clear the DMA Tx buffer */
                    clearTxBuffer = false;
                    /* Clear the TX I2S buffer */
                    ESP_ERROR_CHECK(i2s_zero_dma_buffer(I2S_NUM_0));
                    ESP_LOGI(TAG, "[%s] Clear I2S tx buffer", waveData.name);
                }
            }
        }
         
        /* Check if the I2S module is ready for more data, or the wave just started playing */
        if ( ( (xQueueReceive(gI2sEventQueue, &i2s_evt, 1) == pdPASS) && (i2s_evt.type == I2S_EVENT_TX_DONE) && (wavePlaying == true) ) || (waveStartPlayback == true) )
        {
            /* Set wave start playback flag to false */
            waveStartPlayback = false;

            do
            {
                /* Determine how many bytes to write */
                bytesToWrite = (totalBytesWritten + I2S_MAX_ACTUAL_BYTES_PER_WRITE > waveData.length) ? (waveData.length - totalBytesWritten) : I2S_MAX_ACTUAL_BYTES_PER_WRITE; 

                /* Scale and format the data for audio playback */
                i2cWriteLength = S_DacScaleAndFormatWaveData(i2s_write_buff, (uint8_t*)(waveData.buffer + totalBytesWritten), bytesToWrite);

                /* Write data to I2S DMA buffer */
                ESP_ERROR_CHECK(i2s_write(I2S_NUM_0, i2s_write_buff, i2cWriteLength, &bytesWritten, portMAX_DELAY));

                /* Increment total bytes written */
                totalBytesWritten += (bytesWritten/I2S_DATA_MULTIPLIER);

                /* If we are finished playing the wave */
                if (totalBytesWritten == waveData.length || gStopWavePlayback == true)
                {
                    /* Set wave playing flag to false */
                    wavePlaying = false; 
                    /* Reset the total bytes written */
                    totalBytesWritten = 0;
                    /* Reset the bytes written */
                    bytesWritten = 0;

                    /* If the break loop flag is set */
                    if(gBreakRepeatPlayback == true)
                    {
                        /* Reset the repeat playback flag */
                        gBreakRepeatPlayback = false;
                        /* Force the repeat to zero */
                        waveData.repeat = 0;
                        ESP_LOGI(TAG,"[%s] Break %s playback", __func__, waveData.name);
                    }

                    /* If this was this a request to stop playback */
                    if(gStopWavePlayback == true)
                    {
                        /* Flush Queue */
                        if(xQueueReset(gI2sPlayTaskQueue) != pdPASS)
                        {
                            ESP_LOGI(TAG,"Error resetting I2S play task queue");
                        }

                        /* Reset stop wave playback flag */
                        gStopWavePlayback = false;
                        /* Set flag to clear the DMA Tx buffer */
                        clearTxBuffer = true;
                        ESP_LOGI(TAG,"[%s] Stop %s playback", __func__, waveData.name);
                    }
                    else
                    {
                        /* Check if we need to repeat the wave file */
                        if(waveData.repeat != 0)
                        {
                            /* If this WAV should repeat until a break is sent */
                            if(waveData.repeat != INF_REP)
                            {
                                /* Decrement wave loop count */
                                waveData.repeat--;
                                ESP_LOGI(TAG,"[%s] Repeat %s count: %d", __func__, waveData.name, waveData.repeat+1);
                            }
                            else
                            {
                                /* Report INF_REP */
                                ESP_LOGI(TAG,"[%s] Repeat %s count: INF_REP", __func__, waveData.name);
                            }
                            /* Set wave start playback flag to true */
                            waveStartPlayback = true;
                            /* Set wave playing flag to true */
                            wavePlaying = true;
                        } 
                        else
                        {   
                            /* Set flag to clear the DMA Tx buffer */
                            clearTxBuffer = true;
                            ESP_LOGI(TAG,"[%s] Done %s playback", __func__, waveData.name);
                        }
                    }
                }
            /* As long as we can keep writing data to the DMA buffers */
            } while(bytesWritten != 0);

            //ESP_LOGI(TAG,"Stack high watermark: %d\n", uxTaskGetStackHighWaterMark(NULL));
        }
    }  
}


void DacSetWaveParameters(uint8_t *buffer)
{
    WaveFileHeader_t *header = (WaveFileHeader_t *)buffer;

    /* Stop I2S interface */
    i2s_stop(I2S_NUM_0);

    /* Set WAV parameters */
    i2s_set_clk(I2S_NUM_0, header->sampleRate, header->bitsPerSample, header->numChannels);

    /* Start I2S interface */
    i2s_start(I2S_NUM_0);
}


void DacInitialize(void)
{
    i2s_config_t i2sConfig= {};

    /* Audio parameters */
    i2sConfig.mode = I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN;
    i2sConfig.dma_buf_count         = I2S_MAX_BUFFER_COUNT;
    i2sConfig.dma_buf_len           = I2S_MAX_BYTES_PER_DMA_WRITE;
    i2sConfig.intr_alloc_flags      = ESP_INTR_FLAG_LEVEL1;
    i2sConfig.communication_format  = I2S_COMM_FORMAT_STAND_MSB;
    i2sConfig.channel_format        = I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2sConfig.sample_rate           = I2S_DEFAULT_SAMPLE_RATE;
    i2sConfig.bits_per_sample       = I2S_DEFAULT_SAMPLE_BITS;

    /* Install I2S driver */
    ESP_ERROR_CHECK(i2s_driver_install(I2S_NUM_0, &i2sConfig, 1, &gI2sEventQueue));
    
    /* Disable I2S output pins */
    ESP_ERROR_CHECK(i2s_set_pin(I2S_NUM_0, NULL));
    ESP_ERROR_CHECK(i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN));

    /* Create driver event queue */
    gI2sPlayTaskQueue = xQueueCreate(I2S_MAX_TASK_QUEUE, sizeof(QueueData_t));
    assert(gI2sPlayTaskQueue);

    /* Create DAC play task, priority 4 */
    BaseType_t result = xTaskCreate(S_DacPlayTask, "I2S Task", 2048, NULL, 4, NULL);
    assert(result == pdPASS);
}


void DacPlayWaveData(const char *name, const uint8_t *buffer, size_t length, uint8_t repeat)
{
	QueueData_t waveData = {0};

    /* Set WAV playback settings */
    memcpy(waveData.name, name, sizeof(waveData.name));
    waveData.repeat = repeat; 
    waveData.buffer = buffer;
	waveData.length = length;

    /* Send WAV data to DAC play task */
    if(xQueueSend(gI2sPlayTaskQueue, &waveData, portMAX_DELAY) != pdPASS)
    {
        ESP_LOGI(TAG,"PlayTask Queue is full");
    }
}


void DacStopWavePlayback(void)
{
    gStopWavePlayback = true;
}


void DacBreakRepeatPlayback(void)
{
    gBreakRepeatPlayback = true;
}


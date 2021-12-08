#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2s.h"
#include "dac.h"
#include "common.h"

/**********************************************/
/*                DEFINES                     */
/**********************************************/
#define I2S_DAC_CHANNEL                   I2S_NUM_0
#define I2S_MIXING_CHANNELS               2
#define I2S_DATA_MULTIPLIER               2
#define I2S_MAX_DMA_BYTES_PER_WRITE       128
#define I2S_MAX_ACTUAL_BYTES_PER_WRITE    (I2S_MAX_DMA_BYTES_PER_WRITE/I2S_DATA_MULTIPLIER)
#define I2S_MAX_DMA_BUFFER_COUNT          16
#define I2S_MAX_TASK_QUEUE                25
#define I2S_DEFAULT_SAMPLE_RATE           8000
#define I2S_DEFAULT_SAMPLE_BITS           16


/**********************************************/
/*                TYPEDEFS                    */
/**********************************************/
typedef struct _QueuePlaybackData_t 
{
    char name[10];
    uint8_t repeat;
    const uint8_t *buffer;
    uint32_t length;
} QueuePlaybackData_t;


typedef struct _QueueI2sBufferData_t 
{
    uint8_t *buffer;
    uint32_t length;
} QueueI2sBufferData_t;

/**********************************************/
/*              VARIABLES                     */
/**********************************************/
static const char *TAG = "LOCO-DAC";

static QueueHandle_t gI2sEventQueue = NULL;
static QueueHandle_t gI2sPlayTaskQueue[I2S_MIXING_CHANNELS] = {NULL, NULL};
static QueueHandle_t gI2sWriteTaskQueue[I2S_MIXING_CHANNELS] = {NULL, NULL};
static bool gStopWavePlayback[I2S_MIXING_CHANNELS] = {false,false};
static bool gBreakRepeatPlayback[I2S_MIXING_CHANNELS] = {false,false};
static uint8_t channelId[I2S_MIXING_CHANNELS] = {0,1};

/**********************************************/
/*              FUNCTIONS                     */
/**********************************************/
#if 0
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
#endif

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


static void S_DacWriteToI2sTask(void *arg)
{
    BaseType_t queueRec[I2S_MIXING_CHANNELS] = {pdFALSE, pdFALSE};
    QueueI2sBufferData_t i2sByteBuffer[2] = {{0}};
    uint8_t smallerBuffer = 0;
    uint8_t largerBuffer = 0;
    uint8_t channel = 0;
    uint32_t mixBytes = 0;
    uint32_t bytesWritten = 0;
    uint32_t bufferOffset = 0;
    bool buffersCleared = false;

    ESP_LOGI(TAG, "Starting DAC I2S Write Task");

    while(1)
    {
        /* Delay to allow other tasks to run */
        vTaskDelay(pdMS_TO_TICKS(2));

        /* Check the I2S task queues */
        for(channel=0;channel<I2S_MIXING_CHANNELS;channel++)
        {
            /* Flag which queues have a message */
            queueRec[channel] = xQueueReceive(gI2sWriteTaskQueue[channel], &i2sByteBuffer[channel], 0);
        }

        /* Check if either of the queues have data */
        if( (queueRec[0] == pdTRUE) || (queueRec[1] == pdTRUE) )
        {
            /* Reset the TX buffer cleared flag */
            buffersCleared = false;

            /* Reset the byte offset */
            bufferOffset = 0;

            /* Check if both queues have data */
            if( (queueRec[0] == pdTRUE) && (queueRec[1] == pdTRUE) )
            {
                /* Figure out which buffer is larger */
                largerBuffer = (i2sByteBuffer[0].length > i2sByteBuffer[1].length) ? 0 : 1;
                smallerBuffer = (largerBuffer == 0) ? 1 : 0;

                /* For the size of the smaller buffer, mix the data from the smaller buffer to the larger one (50/50) */
                for(mixBytes=0;mixBytes<i2sByteBuffer[smallerBuffer].length;mixBytes+=4)
                {
                    i2sByteBuffer[largerBuffer].buffer[mixBytes+1] = (i2sByteBuffer[largerBuffer].buffer[mixBytes+1]>>1)+(i2sByteBuffer[smallerBuffer].buffer[mixBytes+1]>>1);
                    i2sByteBuffer[largerBuffer].buffer[mixBytes+3] = (i2sByteBuffer[largerBuffer].buffer[mixBytes+3]>>1)+(i2sByteBuffer[smallerBuffer].buffer[mixBytes+3]>>1);
                }
            }
            /* If only channel 1 has data */
            else 
            {
                if(queueRec[1] == pdTRUE)
                {
                   /* Set channel 1 to the large buffer */ 
                   largerBuffer = 1;
                }
            }
  
            do
            {
                /* Send the data from the larger buffer to the DAC */ 
                ESP_ERROR_CHECK(i2s_write(I2S_DAC_CHANNEL, i2sByteBuffer[largerBuffer].buffer+bufferOffset, i2sByteBuffer[largerBuffer].length, &bytesWritten, portMAX_DELAY));

                /* Subtract the write buffer based on the how many bytes were written */
                i2sByteBuffer[largerBuffer].length -= bytesWritten;
                /* Increment the buffer offset by the bytes that were written */
                bufferOffset += bytesWritten;

            /* Loop until all of the bytes are written to the DAC */
            } while(bytesWritten != 0);
        }
        else
        {
            /* If there is no data left */
            if (buffersCleared == false)
            {
                /* Clear the TX I2S buffer */
                buffersCleared = true;
                ESP_ERROR_CHECK(i2s_zero_dma_buffer(I2S_DAC_CHANNEL));
                ESP_LOGI(TAG, "[%s] Clear I2S tx buffer", __func__);
            }
        }
        //ESP_LOGI(TAG,"Stack high watermark: %d\n", uxTaskGetStackHighWaterMark(NULL));
    }
}


static void S_DacPlaybackTask(void *arg)
{
    uint8_t channel = *(uint8_t *)arg;
    uint32_t bytesWritten = 0;
    uint32_t bytesToWrite = 0;
    bool waveStartPlayback = false;
    bool wavePlaying = false;
    QueuePlaybackData_t waveData = {0};
    QueueI2sBufferData_t i2sWriteData;

    uint32_t totalBytesWritten = 0;
    uint8_t *i2s_write_buff = (uint8_t *) malloc(4096);

    ESP_LOGI(TAG, "Starting DAC Playback Task %d", channel);

    while(1)
    {
        /* Check if there is no WAV already playing */
        if (wavePlaying == false)
        {
            /* Check if there is a WAV to play in the queue*/
            if (xQueueReceive(gI2sPlayTaskQueue[channel], &waveData, portMAX_DELAY))
            {
                /* Reset break playback flag */
                gBreakRepeatPlayback[channel] = false;

                /* Set wave start playback flag to true */
                waveStartPlayback = true;

                /* Set wave playing flag to true */
                wavePlaying = true;

                /* Set total bytes written to 0 */
                totalBytesWritten = 0;
                ESP_LOGI(TAG, "[%s] Start ch%d playback", __func__, channel);
            }
        }
         
        /* Check if the I2S module is ready for more data, or the wave just started playing */
        if ( (wavePlaying == true) || (waveStartPlayback == true) )
        {
            /* Set wave start playback flag to false */
            waveStartPlayback = false;

            do
            {
                /* Determine how many bytes to write */
                bytesToWrite = (totalBytesWritten + I2S_MAX_ACTUAL_BYTES_PER_WRITE > waveData.length) ? (waveData.length - totalBytesWritten) : I2S_MAX_ACTUAL_BYTES_PER_WRITE; 

                /* Scale and format the data for audio playback */
                i2sWriteData.length = S_DacScaleAndFormatWaveData(i2s_write_buff, (uint8_t*)(waveData.buffer + totalBytesWritten), bytesToWrite);
                i2sWriteData.buffer = i2s_write_buff;
    
                /* Send data to I2S mixing queue */
                if(xQueueSend(gI2sWriteTaskQueue[channel], &i2sWriteData, portMAX_DELAY) == pdPASS)
                {
                    /* Set the bytes written to the length of the write buffer */
                    bytesWritten = i2sWriteData.length;
                }
                else
                {
                    /* Set the bytes written to zero */
                    bytesWritten = 0;
                }
 
                /* Increment total bytes written */
                totalBytesWritten += (bytesWritten/I2S_DATA_MULTIPLIER);

                /* If we are finished playing the wave */
                if (totalBytesWritten == waveData.length || gStopWavePlayback[channel] == true)
                {
                    /* Set wave playing flag to false */
                    wavePlaying = false; 

                    /* Reset the total bytes written */
                    totalBytesWritten = 0;

                    /* Reset the bytes written */
                    bytesWritten = 0;

                    /* If the break loop flag is set */
                    if(gBreakRepeatPlayback[channel] == true)
                    {
                        /* Reset the repeat playback flag */
                        gBreakRepeatPlayback[channel] = false;

                        /* Force the repeat to zero */
                        waveData.repeat = 0;
                        ESP_LOGI(TAG,"[%s] Break ch%d playback", __func__, channel);
                    }

                    /* If this was this a request to stop playback */
                    if(gStopWavePlayback[channel] == true)
                    {
                        /* Flush Queue */
                        if(xQueueReset(gI2sPlayTaskQueue[channel]) != pdPASS)
                        {
                            ESP_LOGI(TAG,"[%s] Error resetting ch%d I2S play task queue", __func__, channel);
                        }

                        /* Reset stop wave playback flag */
                        gStopWavePlayback[channel] = false;
                        ESP_LOGI(TAG,"[%s] Stop ch%d playback", __func__, channel);;
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
                                ESP_LOGI(TAG,"[%s] Repeat ch%d count: %d", __func__, channel, waveData.repeat+1);
                            }
                            else
                            {
                                /* Report INF_REP */
                                ESP_LOGI(TAG,"[%s] Repeat ch%d count: INF_REP", __func__, channel);
                            }

                            /* Set wave start playback flag to true */
                            waveStartPlayback = true;

                            /* Set wave playing flag to true */
                            wavePlaying = true;
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
    i2s_stop(I2S_DAC_CHANNEL);

    /* Set WAV parameters */
    i2s_set_clk(I2S_DAC_CHANNEL, header->sampleRate, header->bitsPerSample, header->numChannels);

    /* Start I2S interface */
    i2s_start(I2S_DAC_CHANNEL);
}


void DacInitialize(void)
{
    BaseType_t result;
    uint8_t channel;
    i2s_config_t i2sConfig= {};

    /* Audio parameters */
    i2sConfig.mode = I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN;
    i2sConfig.dma_buf_count         = I2S_MAX_DMA_BUFFER_COUNT;
    i2sConfig.dma_buf_len           = I2S_MAX_DMA_BYTES_PER_WRITE;
    i2sConfig.intr_alloc_flags      = ESP_INTR_FLAG_LEVEL1;
    i2sConfig.communication_format  = I2S_COMM_FORMAT_STAND_MSB;
    i2sConfig.channel_format        = I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2sConfig.sample_rate           = I2S_DEFAULT_SAMPLE_RATE;
    i2sConfig.bits_per_sample       = I2S_DEFAULT_SAMPLE_BITS;

    /* Install I2S driver */
    ESP_ERROR_CHECK(i2s_driver_install(I2S_DAC_CHANNEL, &i2sConfig, 1, &gI2sEventQueue));
    
    /* Disable I2S output pins */
    ESP_ERROR_CHECK(i2s_set_pin(I2S_DAC_CHANNEL, NULL));
    ESP_ERROR_CHECK(i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN));

    for(channel=0;channel<I2S_MIXING_CHANNELS;channel++)
    {
        /* Create playback queues */
        gI2sPlayTaskQueue[channel] = xQueueCreate(I2S_MAX_TASK_QUEUE, sizeof(QueuePlaybackData_t));
        assert(gI2sPlayTaskQueue);

        /* Create I2S buffer data queues */
        gI2sWriteTaskQueue[channel] = xQueueCreate(1, sizeof(QueueI2sBufferData_t));
        assert(gI2sWriteTaskQueue);

        /* Create DAC play task, priority 4 */
        result = xTaskCreate(S_DacPlaybackTask, "DAC Playback Task", 2048, &channelId[channel], TASK_PRIORITY, NULL);
        assert(result == pdPASS);
    }

    /* Create DAC I2S Write task, priority 4 */
    result = xTaskCreate(S_DacWriteToI2sTask, "DAC I2S Write Task", 2048, NULL, TASK_PRIORITY, NULL);
    assert(result == pdPASS);
}


void DacPlayWaveData(uint8_t channel, const char *name, const uint8_t *buffer, size_t length, uint8_t repeat)
{
	QueuePlaybackData_t waveData = {0};

    /* Set WAV playback settings */
    memcpy(waveData.name, name, sizeof(waveData.name));
    waveData.repeat = repeat; 
    waveData.buffer = buffer;
	waveData.length = length;

    /* Send WAV data to DAC play task */
    if(xQueueSend(gI2sPlayTaskQueue[channel], &waveData, portMAX_DELAY) != pdPASS)
    {
        ESP_LOGI(TAG,"[%s] PlayTask Queue is full", __func__);
    }
}


void DacStopWavePlayback(uint8_t channel)
{
    gStopWavePlayback[channel] = true;
}


void DacBreakRepeatPlayback(uint8_t channel)
{
    gBreakRepeatPlayback[channel] = true;
}


#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2s.h"

#define I2S_DATA_MULTIPLIER               2
#define I2S_MAX_BYTES_PER_DMA_WRITE       64
#define I2S_MAX_ACTUAL_BYTES_PER_WRITE    (I2S_MAX_BYTES_PER_DMA_WRITE/I2S_DATA_MULTIPLIER)
#define I2S_MAX_BUFFER_COUNT              32


typedef struct _WaveFileHeader_t
{
    char groupId[4];
    uint32_t totalFileLength;
    char wave[4];
    char formatChunk[4];
    uint32_t lengthOfFormatData;
    uint16_t typeOfFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t bytesPerSecond;
    uint16_t audioFormat;
    uint16_t bitsPerSample;
    char dataChunk[4];
    uint32_t dataSize;
} WaveFileHeader_t;


typedef struct _QueueData_t_t 
{
    const uint8_t *buffer;
    uint32_t length;
} QueueData_t;


static const char *TAG = "MARKLIN-DAC";
static QueueHandle_t i2c_play_task_queue = NULL;
static QueueHandle_t i2s_event_queue = NULL;


static void DacDumpWaveHeaderData(WaveFileHeader_t *header)
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


static uint32_t DacScaleAndFormatWaveData(uint8_t* d_buff, uint8_t* s_buff, uint32_t len)
{
    uint32_t j = 0;

    for (uint32_t i = 0; i < len; i+=4) 
    {
        for(uint32_t m=0; m<I2S_DATA_MULTIPLIER; m++)
        {
            d_buff[j++] = 0x80;
            d_buff[j++] = s_buff[i+1] + 0x80;
            d_buff[j++] = 0x80;
            d_buff[j++] = s_buff[i+3] + 0x80;
       }
    }

    return (len * I2S_DATA_MULTIPLIER);
}



static void DacPlayTask(void *arg)
{
    i2s_event_t i2s_evt;
    uint32_t bytesWritten = 0;
    uint32_t bytesToWrite;
    uint32_t i2s_wr_len;
    int32_t retv;
    uint32_t start = 0, playing = 0;
    QueueData_t data;

    uint32_t totalBytesWritten = 0;
    uint8_t *i2s_write_buff = (uint8_t *) malloc(4096*6);

    while(1)
    {
        if (playing == 0 )
        {
            if (xQueueReceive(i2c_play_task_queue, &data, 1))
            {
                if (playing == 0 )
                {
                    start = 1;
                    playing = 1;
                    totalBytesWritten = 0;
                    ESP_LOGI(TAG, "Starting WAV playback");
                }
            }
        }

        retv = xQueueReceive(i2s_event_queue, &i2s_evt, 1);  // don't let this block for long, as we check for the queue stalling
                       
        if ( ( (retv == pdPASS) && (i2s_evt.type == I2S_EVENT_TX_DONE) && (playing == 1) ) || (start == 1) ) //I2S DMA finish sent 1 buffer
        {
            start = 0;

            do
            {
                bytesToWrite = (totalBytesWritten + I2S_MAX_ACTUAL_BYTES_PER_WRITE > data.length) ? (data.length - totalBytesWritten) : I2S_MAX_ACTUAL_BYTES_PER_WRITE; 
                i2s_wr_len = DacScaleAndFormatWaveData(i2s_write_buff, (uint8_t*)(data.buffer + totalBytesWritten), bytesToWrite);

                ESP_ERROR_CHECK(i2s_write(I2S_NUM_0, i2s_write_buff, i2s_wr_len, &bytesWritten, portMAX_DELAY));

                totalBytesWritten += (bytesWritten/I2S_DATA_MULTIPLIER);

                if (totalBytesWritten == data.length)
                {
                    totalBytesWritten = 0;
                    bytesWritten = 0;
                    playing = 0; 
                    ESP_ERROR_CHECK(i2s_zero_dma_buffer(I2S_NUM_0));
                    ESP_LOGI(TAG, "WAV playback done");
                }

            } while(bytesWritten != 0);
        }
    }  
}


static void DacSetWaveParameters(WaveFileHeader_t *header)
{
    static uint8_t dacInitialized = 0;
    i2s_config_t i2sConfig= {};

    /* Audio parameters */
    i2sConfig.mode = I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN;
    i2sConfig.dma_buf_count         = I2S_MAX_BUFFER_COUNT;
    i2sConfig.dma_buf_len           = I2S_MAX_BYTES_PER_DMA_WRITE;
    i2sConfig.intr_alloc_flags      = ESP_INTR_FLAG_LEVEL1;
    i2sConfig.communication_format  = I2S_COMM_FORMAT_STAND_MSB;
    i2sConfig.channel_format        = I2S_CHANNEL_FMT_RIGHT_LEFT;
    
    i2sConfig.sample_rate           = header->sampleRate;
    i2sConfig.bits_per_sample       = (i2s_bits_per_sample_t)header->bitsPerSample;
    i2sConfig.bits_per_chan         = (i2s_bits_per_chan_t)header->bitsPerSample;

    /* If the DAC is not initialized */
    if(dacInitialized == 0) 
    {
        /* Set DAC initialization flag */
        dacInitialized = 1;

        /* Install I2S driver */
        ESP_ERROR_CHECK(i2s_driver_install(I2S_NUM_0, &i2sConfig, 1, &i2s_event_queue));
        
        /* Disable I2S output pins */
        ESP_ERROR_CHECK(i2s_set_pin(I2S_NUM_0, NULL));
	    ESP_ERROR_CHECK(i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN));
 
        /* Create driver event queue */
	    i2c_play_task_queue = xQueueCreate(1, sizeof(QueueData_t));
	    assert(i2c_play_task_queue);

        /* Create DAC play task, priority 4 */
	    BaseType_t result = xTaskCreate(DacPlayTask, "I2S Task", 2048, NULL, 4, NULL);
	    assert(result == pdPASS);
    }
    else
    {
        /* Stop the I2S interface */
        ESP_ERROR_CHECK(i2s_stop(I2S_NUM_0));

        /* Set the new clock speed */
        ESP_ERROR_CHECK(i2s_set_clk(I2S_NUM_0, header->sampleRate, (uint32_t)(((i2sConfig.bits_per_chan & 0xFFFF) << 16) | (i2sConfig.bits_per_sample & 0xFFFF)), 
                                (header->numChannels == 1) ? I2S_CHANNEL_MONO : I2S_CHANNEL_STEREO));
        /* Start the I2S interface */
        ESP_ERROR_CHECK(i2s_start(I2S_NUM_0));
    }
}


void DacPlayWaveForm(const uint8_t *buffer, size_t length)
{
	QueueData_t data = {};
    BaseType_t result;

    WaveFileHeader_t *header = (WaveFileHeader_t *)buffer;
   
    /* Dump WAV header data */
    //DacDumpWaveHeaderData(header);

    /* Setup audio parameters */
    DacSetWaveParameters(header);

    /* Set WAV buffer and length */
    data.buffer = buffer + sizeof(WaveFileHeader_t);
	data.length = length - sizeof(WaveFileHeader_t);

    /* Send wav data to DAC play task */
	result = xQueueSend(i2c_play_task_queue, &data, portMAX_DELAY);
    assert(result == pdPASS);
}



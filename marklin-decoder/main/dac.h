#ifndef __DAC_H_
#define __DAC_H_

/**********************************************/
/*                DEFINES                     */
/**********************************************/
#define INF_REP     0xFF


/**********************************************/
/*                TYPEDEFS                    */
/**********************************************/
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

/**********************************************/
/*                 APIS                       */
/**********************************************/
void DacInitialize(void);
void DacPlayWaveData(const char *name, const uint8_t* buffer, size_t length, uint8_t repeat);
void DacStopWavePlayback(void);
void DacBreakRepeatPlayback(void);
void DacSetWaveParameters(uint8_t *buffer);
int DacGetMessagesWaiting(void);

#endif

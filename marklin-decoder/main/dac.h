#ifndef __DAC_H_
#define __DAC_H_

#define INF_REP     0xFF

void DacPlayWaveFile(void);
void DacPlayWaveForm(const char *name, const uint8_t* buffer, size_t length, uint8_t repeat);
void DacStopWavePlayback(void);
void DacBreakRepeatPlayback(void);

#endif

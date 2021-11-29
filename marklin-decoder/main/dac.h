#ifndef __DAC_H_
#define __DAC_H_

void example_i2s_init(void);
void DacPlayWaveFile(void);

void DacPlayWaveForm(const uint8_t* buffer, size_t length);

#endif

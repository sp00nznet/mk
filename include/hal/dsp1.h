#ifndef SMK_DSP1_H
#define SMK_DSP1_H

#include <stdint.h>
#include <stdbool.h>

/* DSP-1 is mapped at $6000 (data) and $7000 (status) in banks 00-1F/80-9F for HiROM */
#define DSP1_DATA_ADDR    0x6000
#define DSP1_STATUS_ADDR  0x7000

#define DSP1_MAX_PARAMS   32

typedef struct SMKDsp1 {
    uint8_t  command;
    int16_t  params[DSP1_MAX_PARAMS];
    int16_t  result[DSP1_MAX_PARAMS];
    int      param_index;
    int      param_count;
    int      result_index;
    int      result_count;
    bool     waiting_for_params;
    uint8_t  status;
} SMKDsp1;

extern SMKDsp1 g_dsp1;

void    dsp1_init(void);
void    dsp1_write(uint16_t addr, uint8_t val);
uint8_t dsp1_read(uint16_t addr);

#endif /* SMK_DSP1_H */

/*
 * crc.c
 *
 *  Created on: Jul 29, 2026
 *      Author: SANJU
 */


#include "crc.h"
#include "stm32f4xx.h"

void crc_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_CRCEN;      /* give the peripheral a clock */
}

uint32_t crc_compute(const uint8_t *data, uint32_t len)
{
    CRC->CR = CRC_CR_RESET;                 /* start fresh: internal value = 0xFFFFFFFF */

    uint32_t i = 0;

    /* feed full 32-bit words, bytes packed MSB-first:
       word = b0 b1 b2 b3  ->  (b0<<24)|(b1<<16)|(b2<<8)|b3
       This makes the hardware process bytes in STREAM ORDER,
       which is what Python will imitate. */
    while (i + 4 <= len)
    {
        CRC->DR = ((uint32_t)data[i]   << 24) |
                  ((uint32_t)data[i+1] << 16) |
                  ((uint32_t)data[i+2] <<  8) |
                  ((uint32_t)data[i+3]);
        i += 4;
    }

    /* leftover 1-3 bytes: pad with 0x00 per protocol.md rule */
    if (i < len)
    {
        uint32_t w = 0;
        uint32_t shift = 24;
        while (i < len)
        {
            w |= ((uint32_t)data[i]) << shift;
            shift -= 8;
            i++;
        }
        CRC->DR = w;                        /* missing bytes stay 0x00 */
    }

    return CRC->DR;                         /* reading DR gives the result */
}






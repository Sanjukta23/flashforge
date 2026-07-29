/*
 * crc.h
 *
 *  Created on: Jul 29, 2026
 *      Author: SANJU
 */

#ifndef INC_CRC_H_
#define INC_CRC_H_

#include <stdint.h>

void     crc_init(void);
uint32_t crc_compute(const uint8_t *data, uint32_t len);   /* pads tail with 0x00 */


#endif /* INC_CRC_H_ */

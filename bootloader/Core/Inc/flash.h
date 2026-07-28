/*
 * flash.h
 *
 *  Created on: Jul 28, 2026
 *      Author: SANJU
 */

#ifndef INC_FLASH_H_
#define INC_FLASH_H_

#include <stdint.h>

typedef enum {
    FLASH_OK = 0,
    FLASH_ERR_BUSY_TIMEOUT,
    FLASH_ERR_WRITE,        /* an SR error flag came up */
    FLASH_ERR_VERIFY        /* readback didn't match */
} flash_status_t;

void           flash_unlock(void);
void           flash_lock(void);
flash_status_t flash_erase_sector(uint8_t sector);      /* 0..7 on F446 */
flash_status_t flash_write_word(uint32_t addr, uint32_t data);

#endif /* INC_FLASH_H_ */

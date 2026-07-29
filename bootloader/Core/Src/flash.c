/*
 * flash.c
 *
 *  Created on: Jul 28, 2026
 *      Author: SANJU
 */

#include "flash.h"
#include "stm32f4xx.h"          /* gives us FLASH-> register definitions */

#define BL_FLASH_KEY1  0x45670123UL    /* the two numbers you found in RM0390 */
#define BL_FLASH_KEY2  0xCDEF89ABUL

/* All the error flags in FLASH_SR, OR'd together.
   Writing 1 to these bits CLEARS them (write-1-to-clear — same trick
   you met with UART/CAN status flags). */
#define FLASH_SR_ERRORS  (FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
                          FLASH_SR_PGPERR | FLASH_SR_PGSERR)

static void wait_not_busy(void)
{
    while (FLASH->SR & FLASH_SR_BSY) { }   /* watch the BUSY light */
}

void flash_unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK)         /* only if actually locked */
    {
        FLASH->KEYR = BL_FLASH_KEY1;          /* order matters — */
        FLASH->KEYR = BL_FLASH_KEY2;          /* wrong order = silent re-lock */
    }
}

void flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;            /* close the drawer */
}

flash_status_t flash_erase_sector(uint8_t sector)
{
    wait_not_busy();
    FLASH->SR = FLASH_SR_ERRORS;                    /* clear stale errors */

    FLASH->CR &= ~FLASH_CR_SNB;                     /* clear old sector bits */
    FLASH->CR |=  FLASH_CR_SER                      /* sector-erase mode */
              |  ((uint32_t)sector << FLASH_CR_SNB_Pos);  /* which panel */
    FLASH->CR |=  FLASH_CR_STRT;                    /* GO */

    wait_not_busy();                                /* erase takes ~0.5s! */

    FLASH->CR &= ~FLASH_CR_SER;                     /* leave erase mode */

    return (FLASH->SR & FLASH_SR_ERRORS) ? FLASH_ERR_WRITE : FLASH_OK;
}

flash_status_t flash_write_word(uint32_t addr, uint32_t data)
{
    wait_not_busy();
    FLASH->SR = FLASH_SR_ERRORS;

    FLASH->CR &= ~FLASH_CR_PSIZE;                   /* clear, then set: */
    FLASH->CR |=  FLASH_CR_PSIZE_1;                 /* PSIZE = 10b = x32 (ok at 3.3V) */
    FLASH->CR |=  FLASH_CR_PG;                      /* pen enabled */

    *(volatile uint32_t *)addr = data;              /* THE write — ink on paper */

    wait_not_busy();

    FLASH->CR &= ~FLASH_CR_PG;                      /* pen away */

    if (FLASH->SR & FLASH_SR_ERRORS)  return FLASH_ERR_WRITE;
    if (*(volatile uint32_t *)addr != data) return FLASH_ERR_VERIFY;
    return FLASH_OK;
}

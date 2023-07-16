/**
* @file utility.h
* @author Daniel Starke
* @copyright Copyright 2023 Daniel Starke
* @date 2023-07-09
* @version 2023-07-15
*/
#ifndef _UTILITY_H_
#define _UTILITY_H_

#include <stdint.h>


/** Random seed for `lrng()`. Shall not be zero! */
extern uint32_t lrngSeed;


/**
 * Fill VRAM with the given word.
 *
 * @param[in] value - value to fill with
 * @param[in] address - VRAM address (word counting)
 * @param[in] size - VRAM size in number of bytes
 */
void dmaFillVramWord(const uint16_t value, const uint16_t address, const uint16_t size);


/**
 * Copy source bytes to VRAM low bytes.
 *
 * @param[in] source - source data address
 * @param[in] address - VRAM address (word counting)
 * @param[in] size - VRAM size in number of bytes
 */
void dmaCopyVramLowBytes(const uint8_t * source, const uint16_t address, const uint16_t size);


/**
 * Copy source bytes to VRAM high bytes.
 *
 * @param[in] source - source data address
 * @param[in] address - VRAM address (word counting)
 * @param[in] size - VRAM size in number of bytes
 */
void dmaCopyVramHighBytes(const uint8_t * source, const uint16_t address, const uint16_t size);


/**
 * Linear random number generator. A full period is (2^32)-1.
 *
 * @return random number in the range 0..65535 (16-bit)
 * @see https://www.jstatsoft.org/article/view/v008i14
 * @remarks Operates on the variable `rndSeed` to generate the next value. `lrngSeed` shall not be zero!
 * @remarks Takes 824 clock cycles (~0.6 scanlines).
 */
uint16_t lrng(void);


#endif /* _UTILITY_H_ */

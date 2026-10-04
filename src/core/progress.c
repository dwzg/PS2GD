/* The rules of progress.h. */
#include "progress.h"

uint8_t progress_percent(int32_t x, uint16_t width) PD_BANKED
{
    uint32_t pc;
    if (x <= 0 || !width) return 0;
    /* x * 100 / 65536 rounded down, in two parts that fit 32 bits, then
     * over the width (rounding down twice is rounding down once) */
    pc = ((uint32_t)x >> 16) * 100u + (((uint32_t)x & 0xffffu) * 100u >> 16);
    pc /= width;
    return (uint8_t)(pc > 100 ? 100 : pc);
}

void progress_steps(uint32_t *x, uint16_t width) PD_BANKED
{
    /* x[k] is the least x with x * 100 >= k * width * 65536: k * width *
     * 65536 / 100 rounded up, kept as a quotient and a remainder that each
     * k adds the same step to (no division in the loop: the Game Boy does
     * this while the screen is off) */
    uint32_t q = 0, step = ((uint32_t)width << 16) / 100u;
    uint8_t r = 0, step_r = (uint8_t)(((uint32_t)width << 16) % 100u);
    uint8_t k;
    for (k = 0;; k++) {
        x[k] = q + (r != 0);
        if (k == 100) break;
        q += step;
        r += step_r;
        if (r >= 100) {
            r -= 100;
            q++;
        }
    }
}

void progress_attempt(Progress *p, uint8_t level, uint8_t practice) PD_BANKED
{
    if (practice || level >= PROGRESS_LEVELS) return;
    p->attempts[level]++;
    p->total_attempts++;
}

uint8_t progress_death(Progress *p, uint8_t level, uint8_t practice, uint8_t pc, uint16_t jumps) PD_BANKED
{
    p->total_jumps += jumps;
    if (level >= PROGRESS_LEVELS) return 0;
    if (pc > 99) pc = 99;
    if (practice) {
        if (pc > p->best_practice[level]) p->best_practice[level] = pc;
        return 0;
    }
    if (pc <= p->best[level]) return 0;
    p->best[level] = pc;
    return pc;
}

uint8_t progress_complete(Progress *p, uint8_t level, uint8_t practice, uint8_t coins, uint16_t jumps,
                          uint8_t *first) PD_BANKED
{
    uint8_t before;
    p->total_jumps += jumps;
    *first = 0;
    if (level >= PROGRESS_LEVELS) return 0;
    if (practice) {
        p->best_practice[level] = 100;
        return 0;
    }
    *first = p->best[level] < 100;
    p->best[level] = 100;
    before = p->coins[level];
    p->coins[level] |= coins;
    return (uint8_t)(p->coins[level] & (uint8_t)~before);
}

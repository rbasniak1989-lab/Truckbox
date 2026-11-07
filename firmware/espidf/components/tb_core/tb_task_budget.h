#ifndef TB_TASK_BUDGET_H
#define TB_TASK_BUDGET_H
#include <stdbool.h>
#include <stdint.h>
/* Bound a continuously-ready producer/consumer loop. A receive timeout does
 * not yield if its queue is nonempty. Two blocked ticks guarantee at least one
 * complete tick of lower-priority opportunity regardless of tick phase.
 * This is scheduling, not watchdog feeding or a change in record durability. */
#define TB_TASK_BURST_MAX 16u
#define TB_TASK_BURST_US UINT64_C(4000)
#define TB_TASK_REST_TICKS 2u
typedef struct { uint64_t started_us; unsigned completed; } tb_task_budget_t;
static inline void tb_task_budget_reset(tb_task_budget_t *b, uint64_t now_us)
{ b->started_us=now_us; b->completed=0; }
static inline bool tb_task_budget_due(tb_task_budget_t *b, uint64_t now_us)
{
    ++b->completed;
    return b->completed>=TB_TASK_BURST_MAX || now_us<b->started_us ||
        now_us-b->started_us>=TB_TASK_BURST_US;
}
#endif

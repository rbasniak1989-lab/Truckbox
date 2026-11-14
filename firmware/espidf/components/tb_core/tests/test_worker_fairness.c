/* Real production task loops; deterministic RTOS/clock boundaries only.
 * No claim that this host model measures real ESP32 timing or bus throughput. */
#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tb_task_budget.h"
#define pdTRUE 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) ((ms)/10u)
static jmp_buf done;
static uint64_t now_us, last_idle_us, longest_gap_us, cost_us, tick_us;
static unsigned processed, received, idle_runs, console_runs, mutex_depth;
static void record_gap(void) {
    uint64_t gap=now_us-last_idle_us;
    if (gap>longest_gap_us) longest_gap_us=gap;
}
static inline uint64_t esp_timer_get_time(void) { return now_us; }
static inline void vTaskDelay(unsigned ticks) {
    assert(ticks>=2); assert(!mutex_depth);
    record_gap(); ++idle_runs; ++console_runs;
    /* Worst tick phase: delay(2) still blocks for a complete tick. */
    now_us+=(ticks-1u)*tick_us; last_idle_us=now_us;
}
static int xQueueReceive(void *queue, void *item, unsigned timeout) {
    (void)queue; (void)item; (void)timeout;
    if (received==10000) { record_gap(); longjmp(done,1); }
    ++received;
    return pdTRUE; /* Deliberately never empty: receive must NOT grant IDLE. */
}
#ifdef TEST_STORE
#define STORE_IO_ATTEMPT_LIMIT 3U
static struct { void *queue,*mutex; const char *base_path; bool ready,medium_initialized,initializing,rollback_paused,rollback_drained,io_paused,recovery_complete; unsigned recovery_failures;uint64_t scheduling_yields,next_sequence,highest_committed; } state;
typedef struct { unsigned payload; } queued_record_t;
static void xSemaphoreTake(void *lock,unsigned wait) { (void)lock;(void)wait;++mutex_depth; }
static void xSemaphoreGive(void *lock) { (void)lock;assert(mutex_depth==1);--mutex_depth; }
static unsigned uxQueueMessagesWaiting(void *q){(void)q;return 1;}
static bool recover_after_failure(void) { return true; }
static void prune_segments(void) { }
static bool initialize_medium(void) { state.ready=true;state.medium_initialized=true;return true; }
static void append_record(const queued_record_t *item) {
    (void)item;assert(mutex_depth==1);++processed;now_us+=cost_us;
}
static inline void increment(uint64_t *counter) { ++*counter; }
#define TARGET storage_task
#else
static struct { void *queue; int transport; uint64_t scheduling_yields; } s;
typedef struct { unsigned payload; } packet_t;
static void process_packet(const packet_t *packet) { (void)packet;++processed;now_us+=cost_us; }
static void tbx_j1939_tp_expire(int *transport,uint64_t now) { (void)transport;(void)now; }
static void transport_snapshot(void) { }
static inline void bump(uint64_t *counter) { ++*counter; }
#define TARGET worker_task
#endif
#include "worker_under_test.h"
int main(int argc,char **argv) {
    bool baseline=argc==2 && !strcmp(argv[1],"--expect-starvation");
    const uint64_t costs[]={0,100,2000,30000};
    for (unsigned hz=0;hz<2;++hz) for (unsigned i=0;i<4;++i) {
        now_us=last_idle_us=longest_gap_us=0;
        processed=received=idle_runs=console_runs=mutex_depth=0;
        cost_us=costs[i];tick_us=hz?1000:10000;
        if (!setjmp(done)) TARGET(NULL);
        assert(processed==10000 && !mutex_depth);
        if (baseline) { assert(!idle_runs && !console_runs); }
        else {
            assert(idle_runs>=10000/TB_TASK_BURST_MAX);
            assert(console_runs==idle_runs);
            assert(longest_gap_us<=TB_TASK_BURST_US+cost_us);
            assert(longest_gap_us<UINT64_C(15000000));
        }
    }
#ifdef TEST_STORE
    const char *name="storage_task";
#else
    const char *name="tb_decode";
#endif
    printf("%s: %s; continuously nonempty queue, 100/1000Hz tick, fast/slow items, lower-priority USB/IDLE opportunities, unlocked yield\n",
        name, baseline?"original starvation REPRODUCED":"bounded scheduling PASS");
    return 0;
}

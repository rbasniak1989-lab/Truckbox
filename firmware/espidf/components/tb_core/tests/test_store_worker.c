/* Execute the production writer loop with queue, clock and media fault boundaries. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>
#include <string.h>
#include "tb_task_budget.h"
#define pdTRUE 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(v) (v)
typedef struct{unsigned id;}queued_record_t;
static struct{void *mutex,*queue;const char *base_path;bool ready,medium_initialized,initializing,rollback_paused,rollback_drained;uint64_t highest_committed,next_sequence,scheduling_yields;}state;
static jmp_buf done;
static unsigned received,initializations,recoveries,attempts[11],written[11],mode,mutex;
static bool fault;
static uint64_t now,media_sequence;
static uint64_t esp_timer_get_time(void){return now;}
static void xSemaphoreTake(void *m,unsigned wait){(void)m;(void)wait;assert(!mutex);mutex=1;}
static void xSemaphoreGive(void *m){(void)m;assert(mutex);mutex=0;}
static void increment(uint64_t *v){++*v;}
static bool initialize_medium(void){++initializations;state.ready=true;state.medium_initialized=true;state.base_path="mounted-flash";return true;}
static void prune_segments(void){assert(mutex);}
static bool recover_records(void){assert(mutex);++recoveries;
    if(mode==3 && recoveries==1)return false;
    state.highest_committed=media_sequence;state.next_sequence=101;return true;
}
static unsigned uxQueueMessagesWaiting(void *q){(void)q;return 10-received;}
static int xQueueReceive(void *q,void *p,unsigned timeout){(void)q;(void)timeout;
    if(received==10)return 0;
    ((queued_record_t *)p)->id=++received;return pdTRUE;
}
static void append_record(const queued_record_t *row){assert(mutex && row->id<=10);++attempts[row->id];
    uint64_t seq=state.next_sequence++;now+=2000;
    if(mode && row->id==3 && !fault){fault=true;state.ready=false;
        if(mode==2){++written[row->id];media_sequence=seq;}return;
    }
    assert(!written[row->id]);++written[row->id];state.highest_committed=media_sequence=seq;
    if(row->id==10)state.rollback_paused=true;
}
static void vTaskDelay(unsigned ticks){assert(!mutex);now+=ticks*1000u;
    if(state.rollback_drained){assert(ticks==100 && state.rollback_paused);longjmp(done,1);}
}
#include "worker_under_test.h"
int main(void){for(mode=0;mode<4;mode++){
    memset(&state,0,sizeof(state));memset(written,0,sizeof(written));memset(attempts,0,sizeof(attempts));
    received=initializations=recoveries=mutex=0;fault=false;now=media_sequence=0;state.next_sequence=1;
    if(!setjmp(done))storage_task(NULL);
    for(unsigned i=1;i<=10;i++)assert(written[i]==1);
    assert(received==10 && initializations==1 && !mutex && state.rollback_drained);
    if(mode){assert(recoveries>0);assert(attempts[3]==(mode==2?1u:2u));}
}
puts("Real writer loop: retry uncommitted row, avoid duplicate after ambiguous fsync, repeated read-only recovery, drain barrier and maintenance pause PASS");return 0;}

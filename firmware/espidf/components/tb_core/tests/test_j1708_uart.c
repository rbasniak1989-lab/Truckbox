/* Execute the production UART preparation against a controllable peripheral:
 * empty ring buffer does NOT imply empty hardware FIFO or completed message. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tbx_j1708_active.h"
#define IRAM_ATTR
#define GPIO_NUM_21 21
#define GPIO_NUM_20 20
#define GPIO_MODE_OUTPUT 1
#define SIG_GPIO_OUT_IDX 0
#define UART_NUM_1 1
#define UART_SIGNAL_TXD_INV 1
#define UART_PIN_NO_CHANGE -1
#define ESP_OK 0
#define UART_LL_GET_HW(n) (n)
typedef int esp_err_t;
static struct {
    bool emergency_stop,j1708_ready,edge_ready,rx_partial;
    bool rx_task_busy;
    uint32_t rx_edge_us,rx_edges,uart_start_faults;
    uint64_t last_tx_uart_us;
    int uart_events;
    tbx_j1708_active_t active;
} s;
static uint64_t now;
static unsigned fifo,ring,events,writes,releases,threshold;
static bool rx_low,route_edge,driver_fail,tx_busy;
static int64_t esp_timer_get_time(void){return (int64_t)now;}
static int gpio_get_level(int pin){assert(pin==20);return !rx_low;}
static void gpio_set_level(int pin,int level){assert(pin==21 && level==0);++releases;}
static void gpio_set_direction(int pin,int mode){assert(pin==21 && mode==1);}
static void esp_rom_gpio_connect_out_signal(int pin,int sig,bool a,bool b){assert(pin==21 && sig==0 && !a && !b);}
static uint32_t uart_ll_get_rxfifo_len(int hw){assert(hw==1);return fifo;}
static unsigned uxQueueMessagesWaiting(int q){(void)q;return events;}
static int uart_get_buffered_data_len(int port,size_t *n){assert(port==1);*n=ring;return ESP_OK;}
static int uart_wait_tx_done(int port,unsigned ticks){assert(port==1 && ticks==0);return tx_busy?-1:ESP_OK;}
static int uart_set_rx_full_threshold(int port,unsigned n){assert(port==1);threshold=n;return driver_fail?-1:ESP_OK;}
static int uart_set_line_inverse(int port,unsigned mask){assert(port==1 && mask==UART_SIGNAL_TXD_INV);return ESP_OK;}
static int uart_set_pin(int port,int tx,int rx,int rts,int cts){
    assert(port==1 && tx==21 && rx==20 && rts==-1 && cts==-1);
    if(route_edge){++s.rx_edges;s.rx_edge_us=(uint32_t)now;}
    return ESP_OK;
}
static int uart_tx_chars(int port,const char *bytes,unsigned n){
    assert(port==1 && bytes && n==5);++writes;return (int)n;
}
#include "capture_uart_under_test.h"
static void reset(void){
    memset(&s,0,sizeof(s));now=10000;s.rx_edge_us=1000;s.edge_ready=s.j1708_ready=true;
    s.active.idle_guard_us=5000;fifo=ring=events=writes=releases=0;threshold=64;
    rx_low=route_edge=driver_fail=tx_busy=false;
}
int main(void){
    const uint8_t frame[5]={0xAC,0x80,0x75,0x8C,0xD3};
    reset();fifo=1;
    assert(request_begin_tx(frame,5,NULL)==TBX_J1708_TX_DEFERRED && writes==0);
    reset();ring=1;assert(request_begin_tx(frame,5,NULL)==TBX_J1708_TX_DEFERRED && !writes);
    reset();events=1;assert(request_begin_tx(frame,5,NULL)==TBX_J1708_TX_DEFERRED && !writes);
    reset();s.rx_partial=true;assert(request_begin_tx(frame,5,NULL)==TBX_J1708_TX_DEFERRED && !writes);
    reset();s.rx_task_busy=true;assert(request_begin_tx(frame,5,NULL)==TBX_J1708_TX_DEFERRED && !writes);
    reset();rx_low=true;assert(request_begin_tx(frame,5,NULL)==TBX_J1708_TX_DEFERRED && !writes);
    reset();request_rx_edge(NULL);now+=4999;
    assert(!request_bus_idle((uint32_t)now));++now;assert(request_bus_idle((uint32_t)now));
    reset();route_edge=true;assert(request_begin_tx(frame,5,NULL)==TBX_J1708_TX_DEFERRED);
    assert(!writes && releases && threshold==64); /* Traffic began during routing. */
    reset();driver_fail=true;assert(request_begin_tx(frame,5,NULL)==TBX_J1708_TX_FAULT);
    assert(!writes && releases && s.uart_start_faults==1);
    reset();tx_busy=true;assert(request_begin_tx(frame,5,NULL)==TBX_J1708_TX_DEFERRED && !writes);
    reset();s.emergency_stop=true;assert(request_begin_tx(frame,5,NULL)==TBX_J1708_TX_FAULT && !writes);
    reset();s.rx_edge_us=UINT32_MAX-2000;now=(uint64_t)UINT32_MAX+3000;
    assert(request_bus_idle((uint32_t)now));
    reset();assert(request_begin_tx(frame,5,NULL)==TBX_J1708_TX_STARTED);
    assert(writes==1 && s.last_tx_uart_us==now && threshold==1);
    request_abort_tx(NULL);assert(releases && threshold==64);
    puts("Production UART: FIFO/ring/queue/partial RX, real-edge guard/wrap, preparation race, faults, pin21 inversion, no RX discard and immediate DE release passed");
    return 0;
}

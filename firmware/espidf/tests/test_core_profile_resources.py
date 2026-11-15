"""Compile the actual startup function: disabled RAW must reserve no queue/task."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

class CoreProfileResources(unittest.TestCase):
    def test_raw_tcp_optional_without_disabling_capture_storage_or_bridge(self):
        text=(ROOT/'components/tb_core/core.c').read_text()
        start=text.index('esp_err_t tb_application_start(void)');end=text.index('\nstatic void application_json_mode',start)
        function=text[start:end]
        fixture='''
#include <assert.h>
#include <stddef.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define pdPASS 1
typedef struct {char line[128];size_t length;} live_line_t;
static struct {void *lock,*live;}s;
static int queues,raw_tasks,bridge_tasks,stores,captures;
static void *xSemaphoreCreateMutex(void){return (void *)1;}
static __attribute__((unused)) void *xQueueCreate(int n,size_t bytes){assert(n==64 && bytes==sizeof(live_line_t));++queues;return (void *)1;}
static void __attribute__((unused)) live_task(void *p){(void)p;}
static void bridge_task(void *p){(void)p;}
static int tb_store_init(void){++stores;return 0;}
static int tb_capture_start(void){++captures;return 0;}
static int xTaskCreate(void (*fn)(void *),const char *name,int stack,void *p,int priority,void *h){
 (void)p;(void)priority;(void)h;
 if(!strcmp(name,"tb_tcp")){assert(fn==live_task && stack==4096);++raw_tasks;}
 else {assert(fn==bridge_task && stack==10240);++bridge_tasks;}return pdPASS;
}
'''+function+'''
int main(void){assert(tb_application_start()==0);assert(stores==1 && captures==1 && bridge_tasks==1);
 assert(queues==CONFIG_TB_RAW_TCP && raw_tasks==CONFIG_TB_RAW_TCP);return 0;}
'''
        with tempfile.TemporaryDirectory() as d:
            source=Path(d)/'start.c';source.write_text(fixture)
            for enabled in [0,1]:
                exe=Path(d)/f'test-{enabled}'
                result=subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',f'-DCONFIG_TB_RAW_TCP={enabled}',str(source),'-o',str(exe)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
                subprocess.run([str(exe)],check=True,capture_output=True)

#include "tbx_j1939_tp.h"
#include <string.h>
static uint32_t pgn_of(uint32_t id)
{
    uint32_t pgn=(id>>8)&UINT32_C(0x3ffff);
    if(((id>>16)&0xffu)<240u)pgn&=UINT32_C(0x3ff00);
    return pgn;
}
void tbx_j1939_tp_init(tbx_j1939_tp_t *t)
{
    if(t)memset(t,0,sizeof(*t));
}
void tbx_j1939_tp_expire(tbx_j1939_tp_t *t,uint64_t now)
{
    if(!t)return;
    for(size_t i=0;i<TBX_J1939_TP_SESSIONS;++i) {
        tbx_j1939_tp_session_t *s=&t->sessions[i];
        if(s->active && (now<s->last_ms || now-s->last_ms>TBX_J1939_TP_TIMEOUT_MS)) {
            s->active=false;++t->timeouts;
        }
    }
}
static tbx_j1939_tp_session_t *find(tbx_j1939_tp_t *t,uint8_t sa,uint8_t da)
{
    for(size_t i=0;i<TBX_J1939_TP_SESSIONS;++i)
        if(t->sessions[i].active && t->sessions[i].message.sa==sa &&
           t->sessions[i].message.da==da)return &t->sessions[i];
    return NULL;
}
tbx_j1939_tp_result_t tbx_j1939_tp_feed_capture(tbx_j1939_tp_t *t,uint32_t id,
    bool extended,bool rtr,const uint8_t *d,size_t n,uint64_t now,uint64_t capture_id,
    tbx_j1939_tp_emit_fn emit,void *ctx)
{
    if(!t)return TBX_J1939_TP_REJECTED;
    tbx_j1939_tp_expire(t,now);
    if(!extended || rtr || id>UINT32_C(0x1fffffff))return TBX_J1939_TP_IGNORED;
    uint32_t pgn=pgn_of(id);
    if(pgn!=60416u && pgn!=60160u)return TBX_J1939_TP_IGNORED;
    uint8_t sa=(uint8_t)id,da=(uint8_t)(id>>8);
    tbx_j1939_tp_session_t *s=find(t,sa,da);
    if(n!=8u || !d) {
        if(s)s->active=false;
        ++t->invalid_length;return TBX_J1939_TP_REJECTED;
    }
    if(da!=0xffu) {
        ++t->invalid_destination;return TBX_J1939_TP_REJECTED;
    }
    if(pgn==60416u) {
        /* A new CM from the same source supersedes the old transaction even
         * if rejected; old bytes must never resume under a new announcement. */
        if(s) {s->active=false;++t->restarted;}
        if(d[0]!=0x20u) {++t->unsupported_control;return TBX_J1939_TP_REJECTED;}
        uint32_t target=(uint32_t)d[5]|((uint32_t)d[6]<<8)|((uint32_t)d[7]<<16);
        if(target!=65251u && target!=65249u) {
            ++t->unsupported_pgn;return TBX_J1939_TP_IGNORED;
        }
        unsigned size=(unsigned)d[1]|((unsigned)d[2]<<8);
        unsigned packets=(size+6u)/7u;
        if(size<9u || size>TBX_J1939_TP_MAX_PAYLOAD || d[3]!=packets || d[4]!=0xffu) {
            ++t->invalid_length;return TBX_J1939_TP_REJECTED;
        }
        if(!s)for(size_t i=0;i<TBX_J1939_TP_SESSIONS;++i)
            if(!t->sessions[i].active) {s=&t->sessions[i];break;}
        if(!s) {++t->capacity_rejected;return TBX_J1939_TP_REJECTED;}
        memset(s,0,sizeof(*s));s->active=true;s->expected_sequence=1;s->last_ms=now;
        s->message.pgn=target;s->message.sa=sa;s->message.da=da;
        s->message.length=(uint8_t)size;s->message.packets=(uint8_t)packets;s->message.start_ms=now;s->message.start_capture_id=capture_id;
        ++t->announcements;return TBX_J1939_TP_STARTED;
    }
    if(!s) {++t->orphan_packets;return TBX_J1939_TP_REJECTED;}
    if(d[0]!=s->expected_sequence || !d[0] || d[0]>s->message.packets) {
        s->active=false;++t->invalid_sequence;return TBX_J1939_TP_REJECTED;
    }
    size_t offset=((size_t)d[0]-1u)*7u;
    if(offset>=s->message.length) {
        s->active=false;++t->invalid_sequence;return TBX_J1939_TP_REJECTED;
    }
    size_t bytes=s->message.length-offset;
    if(bytes>7u)bytes=7u;
    memcpy(s->message.data+offset,d+1,bytes);
    s->last_ms=now;++s->expected_sequence;
    if(d[0]!=s->message.packets)return TBX_J1939_TP_PROGRESS;
    s->message.completed_ms=now;s->message.completed_capture_id=capture_id;s->active=false;++t->completed;
    if(emit)emit(&s->message,ctx);
    return TBX_J1939_TP_COMPLETE;
}

tbx_j1939_tp_result_t tbx_j1939_tp_feed(tbx_j1939_tp_t *t,uint32_t id,
    bool extended,bool rtr,const uint8_t *d,size_t n,uint64_t now,
    tbx_j1939_tp_emit_fn emit,void *ctx)
{
    return tbx_j1939_tp_feed_capture(t,id,extended,rtr,d,n,now,0,emit,ctx);
}

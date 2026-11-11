/* Exact function from published 0.2.14 source d7957a2; renamed for comparison. */
static bool healthy_0214(tbx_j1708_active_t *c,uint64_t now)
{
    tbx_j1708_field_t *f=&c->field;
    if(now<f->last_frame_us || now-f->last_frame_us>=UINT64_C(10000000)){
        tbx_j1708_field_fail(c,"PASSIVE_BUS_SILENCE");return false;
    }
    if(__atomic_load_n(&f->audit_lost,__ATOMIC_RELAXED)){
        tbx_j1708_field_fail(c,"FIELD_AUDIT_RECORD_LOST");return false;
    }
    if(now-f->watch_at_us>=UINT64_C(10000000)){
        uint32_t frames=f->frames-f->watch_frames,errors=f->bad_frames-f->watch_errors;
        uint64_t observed=(uint64_t)frames*f->before_duration_us;
        uint64_t expected=(uint64_t)f->before_frames*(now-f->watch_at_us);
        if((errors>3u && (uint64_t)errors*100u>frames) ||
           (f->before_frames && (observed*4u<expected || observed>expected*4u))){
            tbx_j1708_field_fail(c,"PASSIVE_BUS_ERROR_OR_RATE_ANOMALY");return false;
        }
        f->watch_at_us=now;f->watch_frames=f->frames;f->watch_errors=f->bad_frames;
    }
    return true;
}

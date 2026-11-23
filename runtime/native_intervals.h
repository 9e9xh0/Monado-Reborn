// [XCDE] T260: low-overhead A/B frame intervals without enabling the detailed CPU profiler or GPU queries.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "host/utf8_path.h"  // [XCDE] T278: UTF-8 paths through the wide APIs
static inline void xcde_native_interval(uint64_t frame,uint64_t now,const char *gpu,uint32_t driver,int capture) {
    static int initialized; static FILE *output; static uint64_t previous;
    if (!initialized) {
        initialized=1; static char path_env[1100], ini_env[1100];
        const char *path=xcde_utf8_getenv("XCDE_NATIVE_INTERVALS", path_env, sizeof path_env) ? path_env : NULL;
        if (!path) return;
        output=xcde_utf8_fopen(path,"w"); if (!output) return;
        const char *ini=xcde_utf8_getenv("XCDE_INI", ini_env, sizeof ini_env) ? ini_env : NULL;
        FILE *config=xcde_utf8_fopen(ini ? ini : "xcde.ini","rb");
        uint64_t hash=1469598103934665603ull; int byte;
        if (config) { while ((byte=fgetc(config))!=EOF) hash=(hash^(unsigned char)byte)*1099511628211ull; fclose(config); }
        fprintf(output,"#mode=native-intervals\n#gpu=%s\n#driver=%u\n#config=%016llx\nframe,frame_ms,gpu_ms\n",
                gpu,driver,(unsigned long long)hash);
    }
    if (!output) return;
    fprintf(output,"%llu,%.6f,-1\n",(unsigned long long)frame,previous ? (now-previous)/1e6 : 0);
    previous=now;
    if (frame%30==0 || capture) fflush(output);
}

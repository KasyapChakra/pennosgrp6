#include "klogger.h"
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static const char* LOG_DIR = "log";
static const char* LOG_PATH = "log/log";

void klog(const char* fmt, ...) {
    /* ensure log directory exists */
    static int init = 0;
    if (!init) {
        mkdir(LOG_DIR, 0755);        
        unlink(LOG_PATH); // delete the log file from previous runs, if it exists    
        init = 1;
    }

    FILE* fp = fopen(LOG_PATH, "a");
    if (!fp) return; // silent fail

    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);

    fputc('\n', fp);
    fclose(fp);
} 
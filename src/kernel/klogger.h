#ifndef KLOGGER_H_
#define KLOGGER_H_

#include <stdio.h>
#include <stdarg.h>

/* simple kernel logger – writes tab-delimited entries to ./log/log */

void klog(const char* fmt, ...);

#endif /* KLOGGER_H_ */ 
#ifndef UTILS_H_
#define UTILS_H_

#include <stddef.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>

/* very small helper header so that asserts are visible project-wide */

void assert_non_null(const void* ptr, const char* description);
void assert_non_negative(ssize_t val, const char* description);

void prompt(const char* prompt);
int  get_cmd(char* buf, size_t size);

/* parser helpers (defined in utils.c) */
#include "parser.h"
typedef struct parsed_command command_t;
int safe_parse_command(const char* line, command_t** cmd);

#endif /* UTILS_H_ */

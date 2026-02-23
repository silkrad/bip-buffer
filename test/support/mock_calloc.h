#ifndef MOCK_CALLOC_H
#define MOCK_CALLOC_H

#include <stddef.h>

void mock_calloc_reset(void);
void mock_calloc_fail_on_call(int call_number);
void *__wrap_calloc(size_t nmemb, size_t size);
void *__real_calloc(size_t nmemb, size_t size);

#endif

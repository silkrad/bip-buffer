#include "mock_calloc.h"
#include <stddef.h>

static int g_fail_on_call = 0;
static int g_call_count = 0;

void mock_calloc_reset(void) {
    g_fail_on_call = 0;
    g_call_count = 0;
}

void mock_calloc_fail_on_call(int call_number) {
    g_fail_on_call = call_number;
    g_call_count = 0;
}

void *__wrap_calloc(size_t nmemb, size_t size) {
    g_call_count++;
    if (g_fail_on_call > 0 && g_call_count == g_fail_on_call) {
        return NULL;
    }
    return __real_calloc(nmemb, size);
}

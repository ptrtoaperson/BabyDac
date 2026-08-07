#ifndef NO_OS_ALLOC_H_
#define NO_OS_ALLOC_H_

#include <stddef.h>

void *no_os_calloc(size_t nmemb, size_t size);
void no_os_free(void *ptr);

#endif /* NO_OS_ALLOC_H_ */
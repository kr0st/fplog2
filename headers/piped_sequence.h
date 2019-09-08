#ifndef PIPED_SEQUENCE_H
#define PIPED_SEQUENCE_H

#include <ctype.h>

static const char* sequence_pipe_name = "/tmp/fplog2_shared_sequence";
unsigned long long read_sequence_number(size_t timeout = 5000);

#endif // PIPED_SEQUENCE_H

#ifndef PRETTY_PRINTER_H
#define PRETTY_PRINTER_H

#include <stdint.h>
#include <stdio.h>
#include "trace_defs.h"

void print_header(const record_header_t* hdr);
void print_sockaddr(const uint8_t* raw_addr, uint32_t len);
void print_trace_record(const record_header_t* hdr, const void* body);

#endif
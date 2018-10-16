// fincs-edit: this is a shim
#pragma once
#include "pipe/p_format.h"

static inline const char *
util_format_name(enum pipe_format format)
{
   return "PIPE_FORMAT_???";
}

#include "token.h"
#include <stdlib.h>
#include <string.h>

const char* token_type_to_string(token_type p_type) {
  switch (p_type) {
#define X(type, str)                                                                                                   \
  case type:                                                                                                           \
    return str;
    TOKEN_LIST_X(X)
#undef X
  default:
    return "UNKNOWN_TOKEN";
  }
}

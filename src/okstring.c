#include "okstring.h"
#include "utils.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define STRING_FLAGS_MASK 0x00ffffffffffffff

static bool test_flag(uint8_t p_flag, uint64_t p_from) {
  return (p_from & (UINT64_C(1) << (56 + p_flag))) != 0;
}

static void set_flag(uint8_t p_flag, uint64_t* p_info) {
  *p_info |= UINT64_C(1) << (56 + p_flag);
}

static void remove_flag(uint8_t p_flag, uint64_t* p_info) {
  *p_info &= ~(UINT64_C(1) << (56 + p_flag));
}

#define STRING_FLAG_DYNAMIC 1
bool string_init(string* p_string, const char* p_chars, uint64_t p_length, bool p_is_dynamic) {
  p_string->chars = p_chars;
  if (p_length == STRING_CALCULATE_LENGTH) {
    p_length = strlen(p_chars);
    if (p_length > STRING_LENGTH_MAX) {
      goto fail;
    }
  } else if (p_length > STRING_LENGTH_MAX) {
    goto fail;
  }
  p_string->info = p_length & STRING_FLAGS_MASK;
  if (p_is_dynamic) {
    char* chars = (char*)malloc(p_length + 1);
    if (chars == NULL) {
      goto fail;
    }
    memcpy(chars, p_chars, p_length);
    chars[p_length] = '\0';
    p_string->chars = chars;
    set_flag(STRING_FLAG_DYNAMIC, &p_string->info);
  }
  return true;
fail:
  p_string->chars = NULL;
  p_string->info = 0;
  return false;
}

void string_deinit(string* p_string) {
  if (test_flag(STRING_FLAG_DYNAMIC, p_string->info)) {
    free((char*)p_string->chars);
  }
  p_string->chars = NULL;
  p_string->info = 0;
}

uint64_t string_get_length(const string* p_string) {
  return p_string->info & STRING_FLAGS_MASK;
}

bool string_is_dynamic(const string* p_string) {
  return test_flag(STRING_FLAG_DYNAMIC, p_string->info);
}

string create_string(const char* p_chars, uint64_t p_length, bool p_is_dynamic) {
  string str;
  string_init(&str, p_chars, p_length, p_is_dynamic);
  return str;
}

string create_string_from_string_view(const string_view p_string_view) {
  return create_string(p_string_view.chars, string_view_get_length(&p_string_view), true);
}

string create_string_from_cstring_view(cstring_view p_cstring_view) {
  return create_string(p_cstring_view.chars, cstring_view_get_length(&p_cstring_view), true);
}

#define STRING_VIEW_FLAG_CSTR 1

bool string_view_init(string_view* p_string_view, const char* p_chars, uint64_t p_length, bool p_is_cstr_view) {
  if (p_length == STRING_VIEW_CALCULATE_LENGTH &&
      (!p_is_cstr_view || ((p_length = strlen(p_chars)) > STRING_VIEW_LENGTH_MAX))) {
    goto fail;
  } else if (p_length > STRING_VIEW_LENGTH_MAX) {
    goto fail;
  }
  p_string_view->chars = p_chars;
  p_string_view->info = p_length & STRING_FLAGS_MASK;
  if (p_is_cstr_view) {
    set_flag(STRING_VIEW_FLAG_CSTR, &p_string_view->info);
  }
  return true;
fail:
  p_string_view->chars = NULL;
  p_string_view->info = 0;
  return false;
}

void string_view_deinit(string_view* p_string_view) {
  p_string_view->chars = NULL;
  p_string_view->info = 0;
}

uint64_t string_view_get_length(const string_view* p_string_view) {
  return p_string_view->info & STRING_FLAGS_MASK;
}

bool string_view_is_cstring_view(const string_view* p_string_view) {
  return test_flag(STRING_VIEW_FLAG_CSTR, p_string_view->info);
}

string_view create_string_view(const char* p_chars, uint64_t p_length, bool p_is_cstr_view) {
  string_view view;
  string_view_init(&view, p_chars, p_length, p_is_cstr_view);
  return view;
}

string_view create_string_view_from_string(const string p_string) {
  return create_string_view(p_string.chars, string_get_length(&p_string), true);
}

string_view create_string_view_from_cstring_view(const cstring_view p_cstring_view) {
  return create_string_view(p_cstring_view.chars, cstring_view_get_length(&p_cstring_view), true);
}

bool cstring_view_init(cstring_view* p_cstring_view, const char* p_chars, uint64_t p_length) {
  if (p_length == CSTRING_VIEW_CALCULATE_LENGTH) {
    if ((p_length = strlen(p_chars)) > STRING_LENGTH_MAX) {
      goto fail;
    }
    p_cstring_view->info = p_length & STRING_FLAGS_MASK;
  } else if (p_length == CSTRING_VIEW_LAZY_CALCULATE_LENGTH) {
    p_cstring_view->info = p_length;
  } else if (p_length > STRING_LENGTH_MAX) {
    goto fail;
  } else {
    p_cstring_view->info = p_length & STRING_FLAGS_MASK;
  }
  p_cstring_view->chars = p_chars;
  return true;
fail:
  p_cstring_view->chars = NULL;
  p_cstring_view->info = 0;
  return false;
}

void cstring_view_deinit(cstring_view* p_cstring_view) {
  p_cstring_view->chars = NULL;
  p_cstring_view->info = 0;
}

uint64_t cstring_view_get_length(const cstring_view* p_cstring_view) {
  if (p_cstring_view->info == CSTRING_VIEW_LAZY_CALCULATE_LENGTH) {
    const uint64_t len = strlen(p_cstring_view->chars);
    if (len > STRING_LENGTH_MAX) {
      return CSTRING_VIEW_LENGTH_OVERFLOW;
    }
    return len;
  }
  return p_cstring_view->info & STRING_FLAGS_MASK;
}

uint64_t cstring_view_lazy_length(cstring_view* p_cstring_view) {
  if (p_cstring_view->info == CSTRING_VIEW_LAZY_CALCULATE_LENGTH) {
    const uint64_t len = strlen(p_cstring_view->chars);
    if (len > STRING_LENGTH_MAX) {
      return CSTRING_VIEW_LENGTH_OVERFLOW;
    }
    p_cstring_view->info = len & STRING_FLAGS_MASK;
  }
  return p_cstring_view->info;
}

cstring_view create_cstring_view(const char* p_chars, uint64_t p_length) {
  cstring_view cview;
  cstring_view_init(&cview, p_chars, p_length);
  return cview;
}

cstring_view create_cstring_view_from_string(const string p_string) {
  return create_cstring_view(p_string.chars, string_get_length(&p_string));
}

cstring_view create_cstring_view_from_string_view(const string_view p_string_view) {
  if (string_view_is_cstring_view(&p_string_view)) {
    return create_cstring_view(p_string_view.chars, string_view_get_length(&p_string_view));
  }
  return create_cstring_view(NULL, 0);
}

void hashed_string_init(hashed_string* p_hashed_string, const string_view p_view, const hash_t p_hash) {
  p_hashed_string->string = create_string_from_string_view(p_view);
  p_hashed_string->hash = p_hash;
}

void hashed_string_deinit(hashed_string* p_hashed_string) {
  string_deinit(&p_hashed_string->string);
  p_hashed_string->hash = 0;
}

hashed_string create_hashed_string(const string_view p_view, hash_t p_hash) {
  hashed_string hs;
  hashed_string_init(&hs, p_view, p_hash);
  return hs;
}

hashed_string create_hashed_string_hash(const string_view p_view) {
  hashed_string hs;
  hash_t hash = default_hash_str(p_view);
  hashed_string_init(&hs, p_view, hash);
  return hs;
}

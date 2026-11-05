#include "ok_posix.h" // FIRST: the deliberate POSIX opt-in (see its header)

#include "ok_fs.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// the 1 GiB read cap. a bigger "file" is either real (bring a streaming
// api when one exists) or ftell lying about a special node - both refuse.
#define OK_FS_MAX_READ (1u << 30)

bool ok_fs_read_file(const char* p_path, const ok_allocators* p_alloc, ok_fs_buffer* p_out) {
  // a regular file only: fopen alone happily "opens" a directory on
  // linux, and ftell then answers a garbage-huge size (the allocation
  // blast asan caught once already). the probe contract needs the read
  // to fail for anything that is not a file.
  struct stat info;
  if (stat(p_path, &info) != 0 || !S_ISREG(info.st_mode)) {
    return false;
  }
  FILE* file = fopen(p_path, "rb");
  if (file == NULL) {
    return false;
  }
  if (fseek(file, 0L, SEEK_END) != 0) {
    fclose(file);
    return false;
  }
  const long size = ftell(file);
  rewind(file);
  if (size < 0 || (unsigned long)size > OK_FS_MAX_READ) {
    // unreadable or absurd: refuse rather than trust ftell on exotic nodes
    fclose(file);
    return false;
  }
  char* buff = (char*)p_alloc->allocate((size_t)size + 1);
  if (buff == NULL) {
    fclose(file);
    return false;
  }
  const size_t read = fread(buff, sizeof(char), (size_t)size, file);
  fclose(file);
  if (read != (size_t)size) {
    p_alloc->release(buff);
    return false;
  }
  buff[read] = '\0';
  p_out->chars = buff;
  p_out->length = (uint64_t)read;
  return true;
}

bool ok_fs_write_file(const char* p_path, const char* p_data, uint64_t p_length, bool p_append) {
  FILE* file = fopen(p_path, p_append ? "ab" : "wb");
  if (file == NULL) {
    return false;
  }
  const size_t written = p_length > 0 ? fwrite(p_data, sizeof(char), (size_t)p_length, file) : 0;
  const bool ok = written == (size_t)p_length && fflush(file) == 0;
  fclose(file);
  return ok;
}

bool ok_fs_exists(const char* p_path) {
  struct stat info;
  return stat(p_path, &info) == 0;
}

bool ok_fs_is_file(const char* p_path) {
  struct stat info;
  return stat(p_path, &info) == 0 && S_ISREG(info.st_mode);
}

bool ok_fs_is_dir(const char* p_path) {
  struct stat info;
  return stat(p_path, &info) == 0 && S_ISDIR(info.st_mode);
}

bool ok_fs_remove(const char* p_path) {
  if (unlink(p_path) == 0) {
    return true;
  }
  return rmdir(p_path) == 0;
}

bool ok_fs_mkdir(const char* p_path) {
  return mkdir(p_path, 0777) == 0;
}

static int ok_fs_list_cmp(const void* p_a, const void* p_b) {
  const char* const* a = (const char* const*)p_a;
  const char* const* b = (const char* const*)p_b;
  return strcmp(*a, *b);
}

bool ok_fs_list(const char* p_path, const ok_allocators* p_alloc, char*** p_out_names, uint32_t* p_out_count) {
  DIR* dir = opendir(p_path);
  if (dir == NULL) {
    return false;
  }
  char** names = NULL;
  uint32_t count = 0;
  uint32_t capacity = 0;
  bool ok = true;
  const struct dirent* entry;
  while ((entry = readdir(dir)) != NULL) {
    const char* name = entry->d_name;
    if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
      continue; // "." and ".." are structure, not entries
    }
    if (count == capacity) {
      const uint32_t grown = capacity == 0 ? 8 : capacity * 2;
      char** moved = (char**)p_alloc->reallocate(names, (size_t)grown * sizeof(char*));
      if (moved == NULL) {
        ok = false;
        break;
      }
      names = moved;
      capacity = grown;
    }
    const size_t length = strlen(name);
    char* copy = (char*)p_alloc->allocate(length + 1);
    if (copy == NULL) {
      ok = false;
      break;
    }
    memcpy(copy, name, length + 1);
    names[count++] = copy;
  }
  closedir(dir);
  if (ok) {
    if (count > 1) {
      qsort(names, count, sizeof(char*), ok_fs_list_cmp);
    }
    *p_out_names = names;
    *p_out_count = count;
    return true;
  }
  for (uint32_t i = 0; i < count; ++i) {
    p_alloc->release(names[i]);
  }
  p_alloc->release(names);
  return false;
}

bool ok_fs_cwd(char* p_buffer, size_t p_capacity) {
  return getcwd(p_buffer, p_capacity) != NULL;
}

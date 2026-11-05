#ifndef OK_FS_H
#define OK_FS_H

// ok_fs: the ONE door libok's file and directory host calls go through
// (the P4 audit). everything else in the library reaches the filesystem
// through these functions - the module loader's source reads, the dylib
// probe, std.fs - so an embedder auditing the library reads one file to
// know every byte ok touches on disk. the CLI keeps its own POSIX lane
// per its ruling; embedders own their own host code.
//
// buffers come from the caller's raw allocators (no gc - these are
// plain byte buffers, never object roots). every path is a C string; the
// natives above this layer are responsible for rejecting embedded NULs
// before they get here.

#include <ok/ok.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  char* chars;     // p_alloc's allocate; NUL-terminated for tooling
  uint64_t length; // bytes read (the NUL is not counted)
} ok_fs_buffer;

// whole-file read of a REGULAR file. false for anything else: missing,
// unreadable, a directory (fopen happily "opens" those on linux - the
// probe contract refuses), a special node, or absurd size (the 1 GiB
// cap: ftell on exotic nodes answers garbage-huge, and the module lane
// learned that the hard way). the buffer is the caller's to release.
bool ok_fs_read_file(const char* p_path, const ok_allocators* p_alloc, ok_fs_buffer* p_out);

// whole-file write. p_append selects the append mode over the truncate
// mode; both create the file when missing. one fwrite, checked: a short
// write is a failure, not a warning.
bool ok_fs_write_file(const char* p_path, const char* p_data, uint64_t p_length, bool p_append);

bool ok_fs_exists(const char* p_path);
bool ok_fs_is_file(const char* p_path); // a regular file
bool ok_fs_is_dir(const char* p_path);  // a directory (not a symlink to one - stat, not lstat)

// unlink first, rmdir second: a file, or an EMPTY directory, dies; a
// non-empty directory refuses (rmdir's own semantics - the safe shape,
// no recursive deletes exist at this layer, ever).
bool ok_fs_remove(const char* p_path);

// one level, no parents created. 0777 before umask.
bool ok_fs_mkdir(const char* p_path);

// the directory's entry names, sorted by strcmp (the determinism rule),
// "." and ".." excluded. each name and the array itself come from
// p_alloc; the caller releases them all.
bool ok_fs_list(const char* p_path, const ok_allocators* p_alloc, char*** p_out_names, uint32_t* p_out_count);

// the process working directory. false when it is unreadable or does
// not fit p_capacity.
bool ok_fs_cwd(char* p_buffer, size_t p_capacity);

#endif // OK_FS_H

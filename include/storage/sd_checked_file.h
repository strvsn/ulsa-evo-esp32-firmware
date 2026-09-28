#ifndef ULSA_SD_CHECKED_FILE_H
#define ULSA_SD_CHECKED_FILE_H

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

// POSIX VFS calls expose synchronization errors that Arduino File::flush()
// discards. An injected operations table permits real failure-path tests.
struct SdFileOperations {
  int (*openFile)(const char*, int, int);
  ssize_t (*writeFile)(int, const void*, size_t);
  int (*syncFile)(int);
  int (*closeFile)(int);
};

class SdCheckedFile {
public:
  explicit SdCheckedFile(const SdFileOperations* operations = nullptr)
      : _operations(operations ? operations : &systemOperations()) {}
  ~SdCheckedFile() { close(); }
  SdCheckedFile(const SdCheckedFile&) = delete;
  SdCheckedFile& operator=(const SdCheckedFile&) = delete;
  explicit operator bool() const { return _fd >= 0; }
  bool open(const char* absolutePath) {
    if (_fd >= 0) { _error = EBUSY; return false; }
    _fd = _operations->openFile(absolutePath, O_WRONLY | O_CREAT | O_EXCL, 0666);
    _error = _fd < 0 ? errno : 0;
    _size = 0;
    return _fd >= 0;
  }
  size_t write(const uint8_t* data, size_t length) {
    if (_fd < 0) { _error = EBADF; return 0; }
    ssize_t result;
    do { result = _operations->writeFile(_fd, data, length); }
    while (result < 0 && errno == EINTR);
    if (result < 0) { _error = errno; return 0; }
    _size += static_cast<size_t>(result);
    _error = static_cast<size_t>(result) == length ? 0 : EIO;
    return static_cast<size_t>(result);
  }
  size_t print(const char* data) {
    return write(reinterpret_cast<const uint8_t*>(data), strlen(data));
  }
  bool flush() {
    if (_fd < 0) { _error = EBADF; return false; }
    int result;
    do { result = _operations->syncFile(_fd); } while (result < 0 && errno == EINTR);
    _error = result == 0 ? 0 : errno;
    return result == 0;
  }
  bool close() {
    if (_fd < 0) return true;
    const int fd = _fd;
    _fd = -1;
    // Retrying close after EINTR can close an unrelated reused descriptor.
    const int result = _operations->closeFile(fd);
    if (result != 0) _error = errno;
    return result == 0;
  }
  uint64_t size() const { return _size; }
  int error() const { return _error; }
private:
  static int openSystem(const char* path, int flags, int mode) {
    return ::open(path, flags, mode);
  }
  static const SdFileOperations& systemOperations() {
    static const SdFileOperations operations = {openSystem, ::write, ::fsync, ::close};
    return operations;
  }
  const SdFileOperations* _operations;
  int _fd = -1;
  int _error = 0;
  uint64_t _size = 0;
};
#endif

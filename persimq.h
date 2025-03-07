// ---------------------------------------------------------------------------
// PERSIMQ - persistent single process message queue library.
// The library provides simple variable message size file storage abstraction.
//
// Author: MrKirushko
// ---------------------------------------------------------------------------
#ifndef __PERSIMQ_H
#define __PERSIMQ_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const char PERSIMQ_VERSION[]; // PERSIMQ library version

// PERSIMQ object descriptor
typedef struct {
	int fd;
	off_t append_ptr;
	off_t extract_ptr;
	off_t count_bytes;
	off_t count_messages;
	off_t file_size;
} T_PERSIMQ;

typedef enum {
	PERSIMQ_VERBOSITY_SILENT = 0,
	PERSIMQ_VERBOSITY_ERRORS_ONLY,
	PERSIMQ_VERBOSITY_ERRORS_AND_WARNINGS,
	PERSIMQ_VERBOSITY_INFO,
	PERSIMQ_VERBOSITY_DEBUG,
	PERSIMQ_VERBOSITY_DEBUG_2
} T_PERSIMQ_DebugVerbosityLevel;


// Opens a queue file and initializes a T_PERSIMQ struct.
bool   PERSIMQ_open(T_PERSIMQ* mq, char* mqfile_path, off_t mqfile_size);

// Checks if the queue is open.
bool   PERSIMQ_is_open(T_PERSIMQ* mq);

// Writes all the changes and closes a queue file.
// Do not forget to close the queue befor exiting your process or some
// messages may get lost or reappear in the queue!
bool   PERSIMQ_close(T_PERSIMQ* mq);

// Closes a queue file without updating the metadata.
// Always use PERSIMQ_close() for queues which had any write operations
// performed on them or file corruption may ocuur!
bool   PERSIMQ_drop(T_PERSIMQ* mq);

// Clears the queue and writes the changes to the queue file.
bool   PERSIMQ_clear(T_PERSIMQ* mq);

// Writes current queue changes to the queue file.
bool   PERSIMQ_sync(T_PERSIMQ* mq);

// Adds a message to the queue.
bool   PERSIMQ_push(T_PERSIMQ* mq, void* message, size_t message_size);

// Removes the first message from a queue (if available).
bool   PERSIMQ_pop(T_PERSIMQ* mq);

// Removes "pop_count" messages from a queue (if available - otherwise all the messages are
// removed unless the queue is empty in which case "false" is returned).
bool   PERSIMQ_pop_n(T_PERSIMQ* mq, uint64_t pop_count);

// Reads the first message from a queue (if available).
bool   PERSIMQ_get(T_PERSIMQ* mq, void* buffer, size_t buffer_size, size_t* message_size);

// Reads all the messages from a queue (up to the "messages_limit" and up to the buffer size).
bool   PERSIMQ_get_all(T_PERSIMQ* mq, void* buffer, size_t buffer_size, uint64_t max_messages,
					  size_t* total_size, uint64_t* messages_read);

// Checks if there are any messages left in the queue.
bool   PERSIMQ_is_empty(T_PERSIMQ* mq);

// Ruturns the amount of messages left in the queue.
off_t  PERSIMQ_messages_available(T_PERSIMQ* mq);

// Ruturns the amount of data bytes stored in all messages left in the queue.
size_t PERSIMQ_bytes_available(T_PERSIMQ* mq);

// Ruturns the amount of free bytes in the queue.
size_t PERSIMQ_bytes_free(T_PERSIMQ* mq);

// Changes the amount of debug messages to be put out by the library (PERSIMQ_ERRORS_ONLY is the default).
void   PERSIMQ_set_debug_verbosity(T_PERSIMQ_DebugVerbosityLevel verbosity);

#ifdef __cplusplus
}
#endif

#endif

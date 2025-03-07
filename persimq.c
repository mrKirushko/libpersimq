// ---------------------------------------------------------------------------
// PERSIMQ - persistent single process message queue library.
// The library provides simple variable message size file storage abstraction.
//
// Author: MrKirushko
// ---------------------------------------------------------------------------

#include <stdio.h>
#include <inttypes.h> // printf() definitions for stdint
#include <string.h>   // memcpy() and others
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <errno.h>

#include "persimq.h"

const char PERSIMQ_VERSION[] = "0.1";

typedef struct __attribute__((packed)) {
    char ID[4];
    uint64_t append_ptr;
    uint64_t extract_ptr;
    uint64_t count_bytes;
    uint64_t count_messages;
    uint64_t file_size;
    uint8_t crc;
} TFileHeader;

typedef struct {
    char ID[3];
    uint8_t  message_crc;
    uint32_t message_size;
} TMessageHeader;

// CRC8 is used for header integrity checks.
static uint8_t eval_crc8(uint8_t* data, size_t length)
{
    register uint8_t crc = 0;
    for (register size_t byte_idx = 0; byte_idx < length; byte_idx++) {
        crc ^= data[byte_idx];
        for (register uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x8C : (crc << 1);
        }
    }
    return crc;
}

static T_PERSIMQ_DebugVerbosityLevel PERSIMQ_Verbosity = PERSIMQ_VERBOSITY_ERRORS_ONLY;

// The offset to where actual messages begin.
static const off_t wrap_lo_margin = sizeof(TFileHeader);

// An abstraction to handle the buffer margins.
static off_t offset_roll(off_t current_offset, off_t wrap_hi_margin, size_t increment)
{
    off_t sub_offset = (current_offset < wrap_lo_margin) ? wrap_lo_margin : current_offset - wrap_lo_margin;
    off_t sub_margin = wrap_hi_margin - wrap_lo_margin;
    return ((sub_offset + increment) % sub_margin) + wrap_lo_margin;
}

// POSIX read and write operations can get interrupted by signals so
// we may need to repeat the syscalls to get to all the requred data.
static bool multiread(int fd, void* data, size_t length)
{
    if (PERSIMQ_Verbosity == PERSIMQ_VERBOSITY_DEBUG_2) {
        printf("multiread() for %" PRIu64 " bytes...\n", (uint64_t)length); fflush(stdout);
    }
    do {
        ssize_t result = read(fd, data, length);
        if (result <= 0) return false;
        length -= result;
        data += result;
    } while (length);
    return true;
}
static bool multiwrite(int fd, void* data, size_t length)
{
    if (PERSIMQ_Verbosity == PERSIMQ_VERBOSITY_DEBUG_2) {
        printf("multiwrite() for %" PRIu64 " bytes...\n", (uint64_t)length); fflush(stdout);
    }
    do {
        ssize_t result = write(fd, data, length);
        if (result <= 0) return false;
        length -= result;
        data += result;
    } while (length);
    return true;
}

// An abstraction to split I/O operations around buffer file margins.
static bool wrapped_io(int fd, void* data, const size_t length, off_t offset,
    const off_t wrap_hi_margin, off_t* next_offset, const bool do_write)
{
    bool result = true;
    bool (*io_function)(int, void*, size_t) = do_write ? &multiwrite : &multiread;
    // Do a zero increment to make sure that the offset is within bounds.
    offset = offset_roll(offset, wrap_hi_margin, 0);
    size_t first_chunk_size = (wrap_hi_margin - offset);

    if (length >= (wrap_hi_margin - wrap_lo_margin)) {
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            fprintf(stderr, "wrapped_io: Requested message length bigger than the buffer file data section size!\n");
            fflush(stderr);
        }
        return false;
    }

    if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_DEBUG) {
        printf("wrapped_io() called for %" PRIu64
                " bytes (first chunk of %" PRIu64
                " bytes, wrap_hi_margin=%" PRId64
                ", offset=0x%" PRIX64 ")...\n",
            (uint64_t)length, (uint64_t)first_chunk_size, (uint64_t)wrap_hi_margin, (uint64_t)offset); fflush(stdout);
    }

    if (length <= first_chunk_size) {
        // No wrap, single I/O operation is enough
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_DEBUG) {
            printf("Single I/O...\n"); fflush(stdout);
        }
        result &= (lseek(fd, offset, SEEK_SET) >= 0);
        result &= io_function(fd, data, length);
    } else {
        // Partial wrap, 2 I/O oreations are needed
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_DEBUG) {
            printf("Double I/O...\n"); fflush(stdout);
        }
        result &= (lseek(fd, offset, SEEK_SET) >= 0);
        result &= io_function(fd, data, first_chunk_size);
        result &= (lseek(fd, wrap_lo_margin, SEEK_SET) >= 0);
        result &= io_function(fd, data + first_chunk_size, length-first_chunk_size);
    }
    if (next_offset) *next_offset = offset_roll(offset, wrap_hi_margin, length);
    return result;
}

// Opens a queue file and initializes a T_PERSIMQ struct.
bool PERSIMQ_open(T_PERSIMQ* mq, char* mqfile_path, off_t mqfile_size)
{
    // some sanity checks
    if (mqfile_size <= (sizeof(TFileHeader) + sizeof(TMessageHeader) + 1)) {
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            fprintf(stderr, "PERSIMQ_open: file size error!\n"); fflush(stderr);
        }
        return false; // Requestd file size is not big enough to fit anytnig useful
    }

    // Open the file (create if does not exist)
    mode_t oldpermmask = umask(0); // Allow S_IRGRP disabled by the mask by default
    if ((mq->fd = open(mqfile_path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) == -1) {
        mq->fd = 0;
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            perror("PERSIMQ_open: file open");
        }
        return false;
    }
    umask(oldpermmask); // Restore the mask

    #ifdef __unix__
        if (flock(mq->fd, LOCK_EX)) {
            mq->fd = 0;
            if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
                perror("PERSIMQ_open: file lock");
            }
            return false;
        }
    #endif

    // Fill the file with zeroes up to the required size in case it has just been created
    if (ftruncate(mq->fd, mqfile_size)) return false;

    // Initialize the queue structure
    if (lseek(mq->fd, 0, SEEK_SET) < 0) { // Error
        #ifdef __unix__
            flock(mq->fd, LOCK_UN);
        #endif
        close(mq->fd);
        mq->fd = 0;
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            perror("PERSIMQ_open(): file lseek");
        }
        return false;
    }
    TFileHeader header;
    if (!multiread(mq->fd, (void*)&header, sizeof(header))) { // Error
        #ifdef __unix__
            flock(mq->fd, LOCK_UN);
        #endif
        close(mq->fd);
        mq->fd = 0;
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            perror("PERSIMQ_open(): file read");
        }
        return false;
    }
    uint8_t crc = eval_crc8((void*)&header, sizeof(header)-1);
    if (!strncmp((void*)&header.ID, "lPmQ", 4) &&
            (crc == header.crc) &&
            (mqfile_size == header.file_size)) {
        // Header found
        mq->append_ptr = header.append_ptr;
        mq->extract_ptr = header.extract_ptr;
        mq->count_bytes = header.count_bytes;
        mq->count_messages = header.count_messages;
    } else {
        // New queue file or file size changed or damaged header
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_AND_WARNINGS) {
            printf("PERSIMQ_open(): incorrect file header - new or damaged queue file detected!\n");
        }
        mq->append_ptr = sizeof(TFileHeader);
        mq->extract_ptr = sizeof(TFileHeader);
        mq->count_bytes = 0;
        mq->count_messages = 0;
    }
    mq->file_size = mqfile_size;
    if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_INFO) {
        printf("PERSIMQ_open(): append_ptr=0x%" PRIX64 ", extract_ptr=0x%" PRIX64
            ", count_bytes=%" PRId64 ", count_messages=%" PRId64 ", file_size=%" PRId64 ".\n",
            (uint64_t)mq->append_ptr, (uint64_t)mq->extract_ptr,
            (uint64_t)mq->count_bytes, (uint64_t)mq->count_messages, (uint64_t)mq->file_size); fflush(stdout);
    }
    // Done
    return true;
}

bool PERSIMQ_is_open(T_PERSIMQ* mq)
{
    return (mq->fd);
}

// Writes all the changes and closes a queue file.
bool PERSIMQ_close(T_PERSIMQ* mq)
{
    if (!mq->fd) return true; // File already closed, do not attempt to close stdout.
    bool result = true;
    result &= PERSIMQ_sync(mq);
    #ifdef __unix__
        flock(mq->fd, LOCK_UN); // May not be necesary but just in case...
    #endif
    result &= (close(mq->fd) >= 0);
    return result;
}

// Closes a queue file without updating the metadata.
bool PERSIMQ_drop(T_PERSIMQ* mq)
{
    if (!mq->fd) return true; // File already closed, do not attempt to close stdout.
    #ifdef __unix__
        flock(mq->fd, LOCK_UN); // May not be necesary but just in case...
    #endif
    return (close(mq->fd) >= 0);
}

// Clears the queue and writes the changes to the queue file.
bool PERSIMQ_clear(T_PERSIMQ* mq)
{
    if (!mq->fd) return false; // MQ uninitialized, file not opened.
    mq->append_ptr = sizeof(TFileHeader);
    mq->extract_ptr = sizeof(TFileHeader);
    mq->count_bytes = 0;
    mq->count_messages = 0;
    return PERSIMQ_sync(mq);
}

// Writes current queue changes to the queue file.
bool PERSIMQ_sync(T_PERSIMQ* mq)
{
    if (!mq->fd) return false; // MQ uninitialized, file not opened.
    bool result = true;
    result &= (lseek(mq->fd, 0, SEEK_SET) >= 0);
    TFileHeader header = {
        "lPmQ",
        mq->append_ptr,
        mq->extract_ptr,
        mq->count_bytes,
        mq->count_messages,
        mq->file_size,
        0 // crc is filled in below
    };
    header.crc = eval_crc8((void*)&header, sizeof(header)-1);
    result &= multiwrite(mq->fd, (void*)&header, sizeof(header));
    #ifdef __unix__
        result &= (fsync(mq->fd) >= 0);
    #endif
    return result;
}

// Adds a message to the queue.
bool PERSIMQ_push(T_PERSIMQ* mq, void* message, size_t message_size)
{
    if (!mq->fd) { // MQ uninitialized, file not opened.
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            fprintf(stderr, "PERSIMQ_push(): Uninitialized MQ struct provided!\n"); fflush(stderr);
        }
        return false;
    }
    if (PERSIMQ_bytes_free(mq) < (sizeof(TMessageHeader) + message_size)) {
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            fprintf(stderr, "PERSIMQ_push(): MQ does not have enough free space to accept the message!\n"); fflush(stderr);
        }
        return false;
    }
    TMessageHeader header = {
        "PMQ",
        eval_crc8((void*)message, message_size),
        message_size
    };
    if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_DEBUG) {
        printf("Writing header...\n"); fflush(stdout);
    }
    if (!wrapped_io(mq->fd, (void*)&header, sizeof(header), mq->append_ptr, mq->file_size, NULL, true)) {
        #ifdef __unix__
            flock(mq->fd, LOCK_UN);
        #endif
        close(mq->fd);
        mq->fd = 0;
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            perror("PERSIMQ_push(): file write (header)"); fflush(stderr);
        }
        return false;
    }
    if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_DEBUG) {
        printf("Writing data...\n"); fflush(stdout);
    }
    if (!wrapped_io(mq->fd, message, message_size, offset_roll(mq->append_ptr, mq->file_size, sizeof(TMessageHeader)),
            mq->file_size, &mq->append_ptr, true)) {
        #ifdef __unix__
            flock(mq->fd, LOCK_UN);
        #endif
        close(mq->fd);
        mq->fd = 0;
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            perror("PERSIMQ_push(): file write (data)");
        }
        return false;
    }
    mq->count_messages++;
    mq->count_bytes += sizeof(TMessageHeader) + message_size;
    return true;
}

static bool PERSIMQ_read_message_header(T_PERSIMQ* mq, TMessageHeader* header, off_t offset)
{
    if (!mq->fd) {
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            fprintf(stderr, "PERSIMQ_read_message_header(): Uninitialized MQ struct provided!\n"); fflush(stderr);
        }
        return false;
    }
    if (!header) {
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            fprintf(stderr, "PERSIMQ_read_message_header(): incorrect TMessageHeader pointer!\n"); fflush(stderr);
        }
        return false;
    }
    offset = offset_roll(offset, mq->file_size, 0); // Sanitize the offset (should not be needed but just in case...)

    if (!wrapped_io(mq->fd, (void*)header, sizeof(TMessageHeader), offset, mq->file_size, NULL, false)) {
        #ifdef __unix__
            flock(mq->fd, LOCK_UN);
        #endif
        close(mq->fd);
        mq->fd = 0;
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            perror("PERSIMQ_read_message_header(): file read error");
        }
        return false;
    }
    // Check the header
    if (memcmp(header->ID, "PMQ", 3)) { // Broken header
        #ifdef __unix__
            flock(mq->fd, LOCK_UN);
        #endif
        close(mq->fd);
        mq->fd = 0;
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            printf("PERSIMQ_read_message_header(): bad ID (damaged message header at offset 0x%" PRIX64 ")! File closed!\n",
                (int64_t)mq->extract_ptr);
        }
        return false;
    }
    return true;
}


// Removes the first message from a queue (if available).
bool PERSIMQ_pop(T_PERSIMQ* mq)
{
    if (!mq->fd) {
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            fprintf(stderr, "PERSIMQ_pop(): Uninitialized MQ struct provided!\n"); fflush(stderr);
        }
        return false;
    }
    // Check if we have any mesasges left to read
    if (!PERSIMQ_messages_available(mq)) {
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            fprintf(stderr, "PERSIMQ_pop(): Can not remove messages from an empty queue!\n"); fflush(stderr);
        }
        return false;
    }
    // Read the header
    TMessageHeader header;
    if (!PERSIMQ_read_message_header(mq, &header, mq->extract_ptr)) {
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            fprintf(stderr, "PERSIMQ_pop(): Message header read error!\n"); fflush(stderr);
        }
        return false;
    }
    // Roll the indexes
    mq->extract_ptr = offset_roll(mq->extract_ptr, mq->file_size, header.message_size+sizeof(header));
    mq->count_bytes -= header.message_size+sizeof(header);
    mq->count_messages--;
    return true;
}

// Removes "pop_count" messages from a queue (if available - otherwise all the messages are
// removed unless the queue is empty in which case "false" is returned).
bool PERSIMQ_pop_n(T_PERSIMQ* mq, uint64_t pop_count)
{
    if (pop_count >= mq->count_messages) {
        // The quick option - just clear the entire queue
        if ((PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_INFO) && (pop_count > mq->count_messages)) {
            printf("PERSIMQ_get(): Buffer does not contain the requested amount of messages!\n");
        }
        pop_count = mq->count_messages;
        mq->extract_ptr = mq->append_ptr;
        mq->count_bytes = 0;
        mq->count_messages = 0;
        return true;
    } else {
        // The long option - remove them one by one
        bool result = true;
        for (int i = 0; i < pop_count; i++) result &= PERSIMQ_pop(mq);
        return result;
    }
}

static bool PERSIMQ_read_message_data(T_PERSIMQ* mq, void* buffer, size_t buffer_size,
    const size_t message_size, const uint8_t message_crc,
    const off_t extract_ptr)
{
    // Read the message
    if (!wrapped_io(mq->fd, buffer, message_size, extract_ptr, mq->file_size, NULL, false)) {
        #ifdef __unix__
            flock(mq->fd, LOCK_UN);
        #endif
        close(mq->fd);
        mq->fd = 0;
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            perror("PERSIMQ_read_message_data(): file read (data)");
        }
        return false;
    }
    // Check the message
    uint8_t crc = eval_crc8(buffer, message_size);
    if (crc != message_crc) { // Corrupted message detected
        #ifdef __unix__
            flock(mq->fd, LOCK_UN);
        #endif
        close(mq->fd);
        mq->fd = 0;
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            fprintf(stderr, "PERSIMQ_read_message_data(): bad CRC (damaged message at offset 0x%" PRId64 ")! File closed!\n", (int64_t)extract_ptr);
            fflush(stderr);
        }
        return false;
    }
    return true; // Done!
}

// Reads the first message from a queue (if available).
bool PERSIMQ_get(T_PERSIMQ* mq, void* buffer, size_t buffer_size, size_t* message_size)
{
    if (!mq->fd) {
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            fprintf(stderr, "PERSIMQ_pop(): Uninitialized MQ struct provided!\n"); fflush(stderr);
        }
        return false;
    }
    // Check if we have any mesasges left to read
    if (!PERSIMQ_messages_available(mq)) {
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_INFO) {
            printf("PERSIMQ_get(): Buffer does not contain any messages!\n");
        }
        return false;
    }
    // Read the header
    TMessageHeader header;
    if (!PERSIMQ_read_message_header(mq, &header, mq->extract_ptr)) {
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            fprintf(stderr, "PERSIMQ_get_message_by_offset(): Message header read error!\n"); fflush(stderr);
        }
        return false;
    }
    if (message_size) *message_size = header.message_size;
    if (header.message_size > buffer_size) {
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            printf("PERSIMQ_get(): Buffer size is not big enough to fit the message!\n");
        }
        return false;
    }
    return PERSIMQ_read_message_data(mq, buffer, buffer_size,
        header.message_size, header.message_crc,
        mq->extract_ptr+sizeof(header) );
}

// Reads all the messages from a queue (up to the "messages_limit" and up to the buffer size).
bool PERSIMQ_get_all(T_PERSIMQ* mq, void* buffer, size_t buffer_size, uint64_t max_messages,
    size_t* total_size, uint64_t* messages_read)
{
    if (!mq->fd) {
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
            fprintf(stderr, "PERSIMQ_pop(): Uninitialized MQ struct provided!\n"); fflush(stderr);
        }
        return false;
    }
    // Check if we have any mesasges left to read
    if (!PERSIMQ_messages_available(mq)) {
        if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_INFO) {
            printf("PERSIMQ_get(): Buffer does not contain any messages!\n");
        }
        return false;
    }
    // Start extracting the messages
    bool result = true;
    size_t total_size_used = buffer_size;
    uint64_t message_idx = 0;
    off_t current_ptr = mq->extract_ptr;
    while ((message_idx < mq->count_messages) && (message_idx < max_messages)) {
        message_idx++;
        // Get message header
        TMessageHeader header;
        if (!PERSIMQ_read_message_header(mq, &header, current_ptr)) {
            if (PERSIMQ_Verbosity >= PERSIMQ_VERBOSITY_ERRORS_ONLY) {
                fprintf(stderr, "PERSIMQ_get_message_by_offset(): Message header read error!\n"); fflush(stderr);
            }
            return false;
        }
        if (header.message_size > buffer_size) break; // No space left in the user buffer
        // Read the message and adjust the buffer pointer
        result &= PERSIMQ_read_message_data(mq, buffer, buffer_size,
            header.message_size, header.message_crc, current_ptr+sizeof(header));
        if (!result) break; // Do not continue on read errors
        buffer += header.message_size; buffer_size -= header.message_size;
        // Go to the next message
        current_ptr = offset_roll(current_ptr, mq->file_size, header.message_size + sizeof(header));
    }
    if (total_size) *total_size = total_size_used - buffer_size;
    if (messages_read) *messages_read = message_idx;
    return result;
}

// Checks if there are any messages left in the queue.
bool PERSIMQ_is_empty(T_PERSIMQ* mq)
{
    return !(mq->count_bytes);
}

// Ruturns the amount of messages left in the queue.
off_t PERSIMQ_messages_available(T_PERSIMQ* mq)
{
    return mq->count_messages;
}

// Ruturns the amount of data bytes stored in all messages left in the queue.
size_t PERSIMQ_bytes_available(T_PERSIMQ* mq)
{
    return mq->count_bytes - (sizeof(TMessageHeader) * mq->count_messages);
}

// Ruturns the amount of free bytes in the queue.
size_t PERSIMQ_bytes_free(T_PERSIMQ* mq)
{
    return mq->file_size - (mq->count_bytes + sizeof(TFileHeader));
}

// Changes the amount of debug messages to be put out by the library (PERSIMQ_ERRORS_ONLY is the default).
void PERSIMQ_set_debug_verbosity(T_PERSIMQ_DebugVerbosityLevel verbosity)
{
    PERSIMQ_Verbosity = verbosity;
}

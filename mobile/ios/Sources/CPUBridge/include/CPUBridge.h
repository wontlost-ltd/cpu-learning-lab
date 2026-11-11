#ifndef CPU_LEARNING_PHONE_BRIDGE_H
#define CPU_LEARNING_PHONE_BRIDGE_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct CPLJournal CPLJournal;
typedef struct CPLCapture CPLCapture;
typedef struct {uint64_t committed_windows,dropped_frames,discarded_tail,discontinuities;size_t journal_events;} CPLStats;
CPLJournal* cpl_open(const char* path,const char* device,char* error,size_t error_size);
void cpl_close(CPLJournal* journal);
size_t cpl_event_count(CPLJournal* journal);
int cpl_batch(CPLJournal* journal,size_t offset,uint8_t* bytes,size_t capacity,size_t* size,size_t* count,char* error,size_t error_size);
int cpl_ack(CPLJournal* journal,const char* peer,const uint8_t* batch,size_t size,char* error,size_t error_size);
CPLCapture* cpl_capture_create(CPLJournal* journal,uint32_t rate,const char* session,char* error,size_t error_size);
/* One producer: callback only. Fixed memory; no feature extraction, locks, disk I/O or allocation. */
int cpl_push_float(CPLCapture* capture,const float* left,const float* right,size_t frames);
/* Single worker consumer, same owner queue as journal and batch/ack operations. */
int cpl_poll(CPLCapture* capture,size_t max_blocks,CPLStats* stats,char* error,size_t error_size);
int cpl_finish(CPLCapture* capture,CPLStats* stats,char* error,size_t error_size);
void cpl_capture_destroy(CPLCapture* capture);
#ifdef __cplusplus
}
#endif
#endif

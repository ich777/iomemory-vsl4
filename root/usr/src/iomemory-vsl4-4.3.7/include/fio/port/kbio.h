//-----------------------------------------------------------------------------
// Copyright (c) 2006-2014, Fusion-io, Inc.(acquired by SanDisk Corp. 2014)
// Copyright (c) 2014-2016 SanDisk Corp. and/or all its affiliates. All rights reserved.
// Copyright (c) 2016-2017 Western Digital Technologies, Inc. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name of the SanDisk Corp. nor the names of its contributors
//   may be used to endorse or promote products derived from this software
//   without specific prior written permission.
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
// OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//-----------------------------------------------------------------------------
/** @file
 *     NO OS-SPECIFIC REFERENCES ARE TO BE IN THIS FILE
 *
 */
#ifndef __FIO_PORT_KBIO_H__
#define __FIO_PORT_KBIO_H__

#include <fio/common/tsc.h>
#include <fio/port/kscatter.h>
#include <fio/port/ranges.h>
#include <fio/port/list.h>
#include <fio/port/errno.h>
#if ENABLE_LAT_RECORD
#include <fio/port/kfio.h>
#endif

struct fio_bdev;
struct fio_bio_pool;
struct fusion_ioctx;

/*---------------------------------------------------------------------------*/

/// @brief Describes one operation in an io batch for the kernel API.
struct kfio_iovec
{
    fio_block_range_t iov_range; ///< Range of sectors to write to
    uint32_t  iov_op;            ///< The operation (read, write, or discard) for this iovec.
    uint32_t  iov_flags;         ///< Reserved for future use
    uint64_t  iov_base;          ///< The starting address of data buffer (It's a pointer)
};

/// @brief The operation type for each kfio_iovec in a batch
#define FIOV_READ     0x00
#define FIOV_WRITE    0x01
#define FIOV_DISCARD  0x02

/// @brief Flags for the kfio_handle_atomic call
#define FIO_ATOMIC_WRITE_USER_PAGES    0x01  // kfio_iovec.iov_base is in userspace

extern int kfio_handle_atomic(struct fio_bdev *bdev, const struct kfio_iovec *iov,
                              uint32_t iovcnt, uint32_t *sectors_written, uint32_t flags);

/*---------------------------------------------------------------------------*/

typedef struct kfio_bio kfio_bio_t;
typedef void (*kfio_bio_completor_t)(struct kfio_bio *bio, uint64_t bytes_complete, int error);

#define BIO_DIR_READ        0
#define BIO_DIR_WRITE       1

#define KBIO_CMD_INVALID    0
#define KBIO_CMD_READ       1
#define KBIO_CMD_WRITE      2
#define KBIO_CMD_DISCARD    3
#define KBIO_CMD_FLUSH      4

/**
 * Fusion BIO Flags
 *
 * @note With the exception of ACTIVE, DONE, and DUMP flags, all other flags
 * must remain constant after kfio_bio_submit call.
 * @{
 */
#define KBIO_FLG_SYNC       0x001
#define KBIO_FLG_BARRIER    0x002
#define KBIO_FLG_DC         0x004
#define KBIO_FLG_DIRTY      0x008
#define KBIO_FLG_WAIT       0x010
#define KBIO_FLG_NONBLOCK   0x020
#define KBIO_FLG_ACTIVE     0x040
#define KBIO_FLG_DONE       0x080
#define KBIO_FLG_ATOMIC     0x100
#define KBIO_FLG_OWNED      0x200  // Don't wait for or free this bio.  The submitter owns it.
#define KBIO_FLG_DUMP       0x400
#define KBIO_FLG_WIN_DIRECT_IO 0x800 // Only for Windows
/** @} */

#define FIO_BIO_INVALID_SECTOR   0xFFFFFFFFFFFFFFFFULL

#define FUSION_BIO_CLIENT_RESERVED_SIZE      128

#define KBIO_DISCARD_RELATIVE_ALL       (0xFFFFFFFFFFFFFFFFULL)
#define LAT_NUM_BIO_TS 6

struct kfio_bio
{
    struct fio_bdev       *fbio_owner;
    struct fio_bdev       *fbio_bdev;
    struct fio_bio_pool   *fbio_pool;
    struct fusion_ioctx   *fbio_ctx;
    kfio_sg_list_t        *fbio_sgl;              //< The SGL this bio will use (non-discard)
    kfio_dma_map_t        *fbio_dmap;
    fusion_list_entry_t    fbio_list;
    int32_t                fbio_error;            //< Error status of this bio
    uint32_t               fbio_flags;            //< Additional KBIO_FLG options to use
    uint8_t                fbio_cmd;              //< The operation type for this bio
    kfio_cpu_t             fbio_cpu;              //< CPU affinity for this bio
    kfio_bio_completor_t   fbio_completor;        //< Callback on completion of this bio
    uintptr_t              fbio_parameter;        //< Opaque data usable by the completor
    uint64_t               fbio_start_time;       //< Microtime used by stats for R/W requests.
    struct fio_tsc         fbio_start_tsc;        //< The starting time stamp counter.
    uint64_t               fbio_read_zero_bytes;  //< Number of zero bytes read if read bio
    uint64_t               fbio_discard_relative_time;  //< Discard time if discard bio
    fio_block_range_t      fbio_range;            //< Range of sectors to operate on.
    struct kfio_bio       *fbio_next;             //< Next bio in the chain
    uint8_t                fbio_data[FUSION_BIO_CLIENT_RESERVED_SIZE];
#if ENABLE_LAT_RECORD
    uint64_t               perf_timestamps[LAT_NUM_BIO_TS];
#endif
};

#define kfio_bio_sync(bio)  (((bio)->fbio_flags & KBIO_FLG_SYNC) != 0)

#define kfio_bio_chain_for_each(entry, chain) \
    for (entry = (chain); entry != NULL; entry = entry->fbio_next)

extern kfio_bio_t *kfio_bio_alloc(struct fio_bdev *bdev);
extern kfio_bio_t *kfio_bio_try_alloc(struct fio_bdev *bdev);
extern kfio_bio_t *kfio_bio_alloc_chain(struct fio_bdev *bdev, int len);
extern void kfio_bio_free(kfio_bio_t *bio);
extern void kfio_bio_set_cpu(kfio_bio_t *bio, kfio_cpu_t cpu);

__must_use_result
extern int kfio_bio_submit_handle_retryable(kfio_bio_t *bio);
extern int kfio_bio_submit(kfio_bio_t *bio);
extern int kfio_bio_wait(kfio_bio_t *bio);

// Count up the total number of blocks used for anything in a bio.
static inline uint64_t kfio_bio_chain_size_blocks(struct kfio_bio *bio)
{
    struct kfio_bio *sub_bio;
    uint64_t retval = 0;

    kfio_bio_chain_for_each(sub_bio, bio)
    {
        retval += sub_bio->fbio_range.length;
    }

    return retval;
}

// Test if result of kfio_bio_submit_handle_retryable() is a retryable failure or a permanent failure.
//
// If the failure is permanent, the request has already been completed and the caller must *not*
// touch the bio. Otherwise the bio is still live and caller may choose what to do with it.
static inline int kfio_bio_failure_is_retryable(int status)
{
    return (status == -ENOSPC) || (status == -EBUSY);
}

extern void kfio_bio_endio(kfio_bio_t *bio, int error);

extern void kfio_dump_fbio(kfio_bio_t *bio);

static inline void fusion_lat_record_bio(struct kfio_bio *bio, uint32_t checkpoint)
{
#if ENABLE_LAT_RECORD
    bio->perf_timestamps[checkpoint] = kfio_rdtsc();
#endif
}

static inline void fusion_lat_set_bio(struct kfio_bio *bio, uint32_t checkpoint, uint64_t ts)
{
#if ENABLE_LAT_RECORD
    bio->perf_timestamps[checkpoint] = ts;
#endif
}

#endif /* __FIO_PORT_KBIO_H__ */

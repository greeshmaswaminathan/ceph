// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2014 Red Hat
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef CEPH_OS_FS_H
#define CEPH_OS_FS_H

#include <errno.h>
#include <time.h>

#include "acconfig.h"
#ifdef HAVE_LIBAIO
# include <libaio.h>
#endif

#include <string>

#include "include/types.h"
#include "common/Mutex.h"
#include "common/Cond.h"
#include <boost/intrusive/list.hpp>

class FS {
public:
  virtual ~FS() { }

  static FS *create(uint64_t f_type);
  static FS *create_by_fd(int fd);

  virtual const char *get_name() {
    return "generic";
  }

  virtual int set_alloc_hint(int fd, uint64_t hint);

  virtual int get_handle(int fd, std::string *h);
  virtual int open_handle(int mount_fd, const std::string& h, int flags);

  virtual int copy_file_range(int to_fd, uint64_t to_offset,
			      int from_fd,
			      uint64_t from_offset, uint64_t from_len);
  virtual int zero(int fd, uint64_t offset, uint64_t length);

  // -- aio --
#if defined(HAVE_LIBAIO)
  struct aio_t {
    struct iocb iocb;  // must be first element; see shenanigans in aio_queue_t
    void *priv;
    int fd;
    vector<iovec> iov;
    uint64_t offset, length;
    int rval;
    bufferlist bl;  ///< write payload (so that it remains stable for duration)

    boost::intrusive::list_member_hook<> queue_item;

    aio_t(void *p, int f) : priv(p), fd(f), offset(0), length(0), rval(-1000) {
      memset(&iocb, 0, sizeof(iocb));
    }

    void pwritev(uint64_t _offset, uint64_t len) {
      offset = _offset;
      length = len;
      io_prep_pwritev(&iocb, fd, &iov[0], iov.size(), offset);
    }
    void pread(uint64_t _offset, uint64_t len) {
      offset = _offset;
      length = len;
      bufferptr p = buffer::create_page_aligned(length);
      io_prep_pread(&iocb, fd, p.c_str(), length, offset);
      bl.append(std::move(p));
    }

    int get_return_value() {
      return rval;
    }
  };

  typedef boost::intrusive::list<
    aio_t,
    boost::intrusive::member_hook<
      aio_t,
      boost::intrusive::list_member_hook<>,
      &aio_t::queue_item> > aio_list_t;

  struct aio_queue_t {
    int max_iodepth;
    io_context_t ctx;


    explicit aio_queue_t(unsigned max_iodepth)
      : max_iodepth(max_iodepth),
	ctx(0) {
    }
    ~aio_queue_t() {
      assert(ctx == 0);
    }

    int init() {
      assert(ctx == 0);
      return io_setup(max_iodepth, &ctx);
    }
    void shutdown() {
      if (ctx) {
	int r = io_destroy(ctx);
	assert(r == 0);
	ctx = 0;
      }
    }

    int submit(aio_t &aio, int *retries);
    int get_next_completed(int timeout_ms, aio_t **paio, int max);
  };
#endif
};

#endif

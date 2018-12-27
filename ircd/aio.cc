// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

#include <sys/syscall.h>
#include <sys/eventfd.h>
#include <ircd/asio.h>
#include "aio.h"

namespace ircd::fs::aio
{
	static int reqprio(int);
}

///////////////////////////////////////////////////////////////////////////////
//
// fs/aio.h
//
// The contents of this section override weak symbols in ircd/fs.cc when this
// unit is conditionally compiled and linked on AIO-supporting platforms. On
// non-supporting platforms, or for items not listed here, the definitions in
// ircd/fs.cc are the default.

decltype(ircd::fs::aio::SUPPORT)
ircd::fs::aio::SUPPORT
{
	true
};

/// True if IOCB_CMD_FSYNC is supported by AIO. If this is false then
/// fs::fsync_opts::async=true flag is ignored.
decltype(ircd::fs::aio::SUPPORT_FSYNC)
ircd::fs::aio::SUPPORT_FSYNC
{
	false //TODO: get this info
};

/// True if IOCB_CMD_FDSYNC is supported by AIO. If this is false then
/// fs::fsync_opts::async=true flag is ignored.
decltype(ircd::fs::aio::SUPPORT_FDSYNC)
ircd::fs::aio::SUPPORT_FDSYNC
{
	false //TODO: get this info
};

decltype(ircd::fs::aio::MAX_EVENTS)
ircd::fs::aio::MAX_EVENTS
{
	128L
};

decltype(ircd::fs::aio::MAX_REQPRIO)
ircd::fs::aio::MAX_REQPRIO
{
	info::aio_reqprio_max
};

//
// init
//

ircd::fs::aio::init::init()
{
	assert(!context);
	if(!bool(aio::enable))
		return;

	context = new kernel{};
}

ircd::fs::aio::init::~init()
noexcept
{
	delete context;
	context = nullptr;
}

///////////////////////////////////////////////////////////////////////////////
//
// ircd/aio.h
//

//
// request::fsync
//

ircd::fs::aio::request::fsync::fsync(const int &fd,
                                     const sync_opts &opts)
:request{fd}
{
	sopts = &opts;

	aio_reqprio = reqprio(opts.priority);
	aio_lio_opcode = IOCB_CMD_FSYNC;

	aio_buf = 0;
	aio_nbytes = 0;
	aio_offset = 0;
}

void
ircd::fs::aio::fsync(const fd &fd,
                     const sync_opts &opts)
{
	aio::request::fsync request
	{
		fd, opts
	};

	request();
}

//
// request::fdsync
//

ircd::fs::aio::request::fdsync::fdsync(const int &fd,
                                       const sync_opts &opts)
:request{fd}
{
	sopts = &opts;

	aio_reqprio = reqprio(opts.priority);
	aio_lio_opcode = IOCB_CMD_FDSYNC;

	aio_buf = 0;
	aio_nbytes = 0;
	aio_offset = 0;
}

void
ircd::fs::aio::fdsync(const fd &fd,
                      const sync_opts &opts)
{
	aio::request::fdsync request
	{
		fd, opts
	};

	request();
}

//
// request::read
//

ircd::fs::aio::request::read::read(const int &fd,
                                   const const_iovec_view &iov,
                                   const read_opts &opts)
:request{fd}
{
	ropts = &opts;

	aio_reqprio = reqprio(opts.priority);
	aio_lio_opcode = IOCB_CMD_PREADV;

	aio_buf = uintptr_t(iov.data());
	aio_nbytes = iov.size();
	aio_offset = opts.offset;
}

size_t
ircd::fs::aio::read(const fd &fd,
                    const const_iovec_view &bufs,
                    const read_opts &opts)
{
	aio::request::read request
	{
		fd, bufs, opts
	};

	stats.cur_reads++;
	stats.max_reads = std::max(stats.max_reads, stats.cur_reads);
	const unwind dec{[]
	{
		stats.cur_reads--;
	}};

	// Make request; blocks ircd::ctx until completed or throw.
	const size_t bytes
	{
		request()
	};

	stats.bytes_read += bytes;
	stats.reads++;
	return bytes;
}

//
// request::write
//

ircd::fs::aio::request::write::write(const int &fd,
                                     const const_iovec_view &iov,
                                     const write_opts &opts)
:request{fd}
{
	wopts = &opts;

	aio_reqprio = reqprio(opts.priority);
	aio_lio_opcode = IOCB_CMD_PWRITEV;

	aio_buf = uintptr_t(iov.data());
	aio_nbytes = iov.size();
	aio_offset = opts.offset;
}

size_t
ircd::fs::aio::write(const fd &fd,
                     const const_iovec_view &bufs,
                     const write_opts &opts)
{
	aio::request::write request
	{
		fd, bufs, opts
	};

	#ifndef _NDEBUG
	const size_t req_bytes
	{
		fs::bytes(request.iovec())
	};
	#endif

	stats.cur_bytes_write += req_bytes;
	stats.cur_writes++;
	stats.max_writes = std::max(stats.max_writes, stats.cur_writes);
	const unwind dec{[&req_bytes]
	{
		stats.cur_bytes_write -= req_bytes;
		stats.cur_writes--;
	}};

	// Make the request; ircd::ctx blocks here. Throws on error
	const size_t bytes
	{
		request()
	};

	// Does linux ever not complete all bytes for an AIO?
	assert(bytes == req_bytes);

	stats.bytes_write += bytes;
	stats.writes++;
	return bytes;
}

//
// request::prefetch
//

void
ircd::fs::aio::prefetch(const fd &fd,
                        const size_t &size,
                        const read_opts &opts)
{

}

//
// request
//

ircd::fs::aio::request::request(const int &fd)
:iocb{0}
{
	assert(context);
	assert(ctx::current);

	aio_flags = IOCB_FLAG_RESFD;
	aio_resfd = context->resfd.native_handle();
	aio_fildes = fd;
	aio_data = uintptr_t(this);
}

ircd::fs::aio::request::~request()
noexcept
{
}

/// Cancel a request. The handler callstack is invoked directly from here
/// which means any callback will be invoked or ctx will be notified if
/// appropriate.
void
ircd::fs::aio::request::cancel()
{
	assert(context);
	context->cancel(*this);
	stats.bytes_cancel += bytes(iovec());
	stats.cancel++;
}

/// Submit a request and properly yield the ircd::ctx. When this returns the
/// result will be available or an exception will be thrown.
size_t
ircd::fs::aio::request::operator()()
try
{
	assert(context);
	assert(ctx::current);
	assert(waiter == ctx::current);

	const size_t submitted_bytes
	{
		bytes(iovec())
	};

	// Submit to kernel
	context->submit(*this);

	// Update stats for submission phase
	stats.bytes_requests += submitted_bytes;
	stats.requests++;

	const auto &curcnt(stats.requests - stats.complete);
	stats.max_requests = std::max(stats.max_requests, curcnt);

	// Block for completion
	while(retval == std::numeric_limits<ssize_t>::min())
		ctx::wait();

	assert(retval <= ssize_t(submitted_bytes));

	// Update stats for completion phase.
	stats.bytes_complete += submitted_bytes;
	stats.complete++;

	if(retval == -1)
	{
		stats.bytes_errors += submitted_bytes;
		stats.errors++;

		throw fs::error
		{
			make_error_code(errcode)
		};
	}

	return size_t(retval);
}
catch(const ctx::interrupted &e)
{
	// When the ctx is interrupted we're obligated to cancel the request.
	// The handler callstack is invoked directly from here by cancel() for
	// what it's worth but we rethrow the interrupt anyway.
	cancel();
	throw;
}
catch(const ctx::terminated &)
{
	cancel();
	throw;
}

ircd::fs::const_iovec_view
ircd::fs::aio::request::iovec()
const
{
	return
	{
		reinterpret_cast<const ::iovec *>(aio_buf), aio_nbytes
	};
}

//
// kernel
//

//
// kernel::kernel
//

ircd::fs::aio::kernel::kernel()
try
:queue
{
	MAX_EVENTS
}
,event
{
	MAX_EVENTS
}
,resfd
{
	ios::get(), int(syscall(::eventfd, semval, EFD_NONBLOCK))
}
{
	syscall<SYS_io_setup>(MAX_EVENTS, &idp);
	set_handle();
	log::debug
	{
		"Established AIO context %p", this
	};
}
catch(const std::exception &e)
{
	log::error
	{
		"Error starting AIO context %p :%s",
		(const void *)this,
		e.what()
	};
}

ircd::fs::aio::kernel::~kernel()
noexcept try
{
	assert(qcount == 0);
	const ctx::uninterruptible::nothrow ui;

	interrupt();
	wait();

	boost::system::error_code ec;
	resfd.close(ec);

	syscall<SYS_io_destroy>(idp);
}
catch(const std::exception &e)
{
	log::critical
	{
		"Error shutting down AIO context %p :%s",
		(const void *)this,
		e.what()
	};
}

bool
ircd::fs::aio::kernel::interrupt()
{
	if(!resfd.is_open())
		return false;

	resfd.cancel();
	return true;
}

bool
ircd::fs::aio::kernel::wait()
{
	if(!resfd.is_open())
		return false;

	log::debug
	{
		"Waiting for AIO context %p", this
	};

	dock.wait([this]
	{
		return semval == uint64_t(-1);
	});

	return true;
}

void
ircd::fs::aio::kernel::cancel(request &request)
{
	const auto &cb
	{
		static_cast<iocb *>(&request)
	};

	const auto eit
	{
		std::remove(begin(queue), end(queue), cb)
	};

	const auto qcount
	{
		size_t(std::distance(begin(queue), eit))
	};

	// We know something was erased if the qcount no longer matches
	const bool erased_from_queue
	{
		this->qcount > qcount
	};

	// Make the qcount accurate again after any erasure.
	assert(!erased_from_queue || this->qcount == qcount + 1);
	assert(erased_from_queue || this->qcount == qcount);
	if(erased_from_queue)
	{
		this->qcount--;
		stats.cur_queued--;
	}

	// Setup an io_event result which we will handle as a normal event
	// immediately on this stack. We create our own cancel result if
	// the request was not yet submitted to the kernel so the handler
	// remains agnostic to our userspace queues.
	io_event result {0};
	if(erased_from_queue)
	{
		result.data = cb->aio_data;
		result.obj = uintptr_t(cb);
		result.res = -1;
		result.res2 = ECANCELED;
	}
	else syscall_nointr<SYS_io_cancel>(idp, cb, &result);

	handle_event(result);
}

void
ircd::fs::aio::kernel::submit(request &request)
try
{
	assert(qcount < queue.size());
	assert(request.aio_data == uintptr_t(&request));

	const ctx::critical_assertion ca;
	queue.at(qcount++) = static_cast<iocb *>(&request);
	stats.cur_queued++;

	const bool flush_now
	{
		// The queue has reached the configured size
		qcount >= size_t(max_submit)

		// The queue has reached its maximum size
		|| qcount >= queue.size()

		// The request causes serialization. This is considered true for all
		// non-reading events, even for different files and locations. It may
		// be possible to optimize this condition.
		|| request.aio_lio_opcode != IOCB_CMD_PREADV
		|| request.ropts->nodelay
	};

	if(flush_now)
		return flush();

	if(qcount == 1)
		ircd::post(std::bind(&kernel::chase, this));
}
catch(const std::exception &e)
{
	throw assertive
	{
		"AIO(%p) submit(): %s", this, e.what()
	};
}

/// The chaser is posted to the IRCd event loop after the first
/// request is queued. Ideally more requests will queue up before
/// the chaser is executed.
void
ircd::fs::aio::kernel::chase()
noexcept
{
	if(qcount)
	{
		flush();
		stats.chased_submits++;
	}
}

/// The flusher submits all queued requests and resets the count.
void
ircd::fs::aio::kernel::flush()
noexcept try
{
	assert(qcount > 0);
	syscall<SYS_io_submit>(idp, qcount, queue.data());
	stats.maxed_submits += qcount >= size_t(max_submit);
	stats.single_submits += qcount == 1;
	stats.cur_queued -= qcount;
	stats.submits++;
	qcount = 0;
}
catch(const std::exception &e)
{
	throw assertive
	{
		"AIO(%p) flush(%zu): %s", this, qcount, e.what()
	};
}

void
ircd::fs::aio::kernel::set_handle()
{
	semval = 0;

	const asio::mutable_buffers_1 bufs
	{
		&semval, sizeof(semval)
	};

	auto handler
	{
		std::bind(&kernel::handle, this, ph::_1, ph::_2)
	};

	asio::async_read(resfd, bufs, std::move(handler));
}

/// Handle notifications that requests are complete.
void
ircd::fs::aio::kernel::handle(const boost::system::error_code &ec,
                              const size_t bytes)
noexcept try
{
	namespace errc = boost::system::errc;

	assert((bytes == 8 && !ec && semval >= 1) || (bytes == 0 && ec));
	assert(!ec || ec.category() == asio::error::get_system_category());

	switch(ec.value())
	{
		case errc::success:
			handle_events();
			break;

		case errc::operation_canceled:
			throw ctx::interrupted();

		default:
			throw_system_error(ec);
	}

	set_handle();
}
catch(const ctx::interrupted &)
{
	log::debug
	{
		"AIO context %p interrupted", this
	};

	semval = -1;
	dock.notify_all();
}

void
ircd::fs::aio::kernel::handle_events()
noexcept try
{
	assert(!ctx::current);

	// The number of completed requests available in events[]. This syscall
	// is restarted on EINTR. After restart, it may or may not find any ready
	// events but it never blocks to do so.
	const auto count
	{
		syscall_nointr<SYS_io_getevents>(idp, 0, event.size(), event.data(), nullptr)
	};

	// The count should be at least 1 event. The only reason to return 0 might
	// be related to an INTR; this assert will find out and may be commented.
	//assert(count > 0);
	assert(count >= 0);

	// Update any stats.
	stats.events += count;
	stats.handles++;

	for(ssize_t i(0); i < count; ++i)
		handle_event(event[i]);
}
catch(const std::exception &e)
{
	log::error
	{
		"AIO(%p) handle_events: %s",
		this,
		e.what()
	};
}

void
ircd::fs::aio::kernel::handle_event(const io_event &event)
noexcept try
{
	// Our extended control block is passed in event.data
	auto &request
	{
		*reinterpret_cast<aio::request *>(event.data)
	};

	auto *const iocb(reinterpret_cast<struct ::iocb *>(event.obj));
	assert(iocb == static_cast<struct ::iocb *>(&request));
	assert(reinterpret_cast<aio::request *>(iocb->aio_data) == &request);
	assert(event.res2 >= 0);
	assert(event.res == -1 || event.res2 == 0);

	// Set result indicators
	request.retval = std::max(event.res, -1LL);
	request.errcode = event.res >= -1? event.res2 : std::abs(event.res);

	// Notify the waiting context. Note that we are on the main async stack
	// but it is safe to notify from here. The waiter may be null if it left.
	assert(!request.waiter || request.waiter != ctx::current);
	assert(ctx::current == nullptr);
	if(likely(request.waiter))
		ctx::notify(*request.waiter);
}
catch(const std::exception &e)
{
	log::critical
	{
		"Unhandled request(%lu) event(%p) error: %s",
		event.data,
		&event,
		e.what()
	};
}

//
// internal util
//

/// Translate an ircd::fs opts priority integer to an AIO priority integer.
/// The ircd::fs priority integer is like a nice value. The AIO value is
/// positive [0, MAX_REQPRIO]. This function takes an ircd::fs value and
/// shifts it to the AIO value.
int
ircd::fs::aio::reqprio(int input)
{
	static const auto median
	{
		int(MAX_REQPRIO / 2)
	};

	input = std::max(input, 0 - median);
	input = std::min(input, median);
	input = MAX_REQPRIO - (input + median);
	assert(input >= 0 && input <= int(MAX_REQPRIO));
	return input;
}

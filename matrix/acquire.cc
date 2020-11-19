// The Construct
//
// Copyright (C) The Construct Developers, Authors & Contributors
// Copyright (C) 2016-2020 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

decltype(ircd::m::acquire::log)
ircd::m::acquire::log
{
	"m.acquire"
};

template<>
decltype(ircd::util::instance_list<ircd::m::acquire>::allocator)
ircd::util::instance_list<ircd::m::acquire>::allocator
{};

template<>
decltype(ircd::util::instance_list<ircd::m::acquire>::list)
ircd::util::instance_list<ircd::m::acquire>::list
{
	allocator
};

//
// execute::execute
//

ircd::m::acquire::acquire::acquire(const struct opts &opts)
:opts{opts}
{
	// Branch to acquire head
	if(opts.head)
		acquire_head();

	// Branch to acquire missing
	if(opts.missing)
		acquire_missing();

	// Complete all work before returning, otherwise everything
	// will be cancelled on unwind.
	while(!fetching.empty())
		while(handle());
}

ircd::m::acquire::~acquire()
noexcept
{
}

void
ircd::m::acquire::acquire_missing()
{
	event::idx ref_min
	{
		opts.ref.first
	};

	for(size_t i(0); i < opts.rounds; ++i)
	{
		if(!fetch_missing(ref_min))
			break;

		if(ref_min > opts.ref.second)
			break;
	}
}

bool
ircd::m::acquire::fetch_missing(event::idx &ref_min)
{
	const auto top
	{
		m::top(opts.room.room_id)
	};

	const auto &[top_id, top_depth, top_idx]
	{
		top
	};

	auto depth_range
	{
		opts.depth
	};

	if(!depth_range.first && opts.viewport_size)
		depth_range =
		{
			m::viewport(opts.room).first, depth_range.second
		};

	if(!depth_range.second)
		depth_range.second = top_depth;

	if(size_t(depth_range.second - depth_range.first) < opts.viewport_size)
		depth_range.first -= std::min(long(opts.viewport_size), depth_range.first);

	m::room::events::missing missing
	{
		opts.room
	};

	bool ret(false);
	event::idx ref_top(ref_min);
	missing.for_each(depth_range, [this, &top, &ref_min, &ref_top, &ret]
	(const event::id &event_id, const int64_t &ref_depth, const event::idx &ref_idx)
	{
		if(ctx::interruption_requested())
			return false;

		if(ref_idx < opts.ref.first || ref_idx < ref_min)
			return true;

		if(ref_idx > opts.ref.second)
			return true;

		// Branch if we have to measure the viewportion
		if(opts.viewport_size)
		{
			const m::event::idx_range idx_range
			{
				std::min(ref_idx, std::get<event::idx>(top)),
				std::max(ref_idx, std::get<event::idx>(top)),
			};

			// Bail if this event sits above the viewport.
			if(m::room::events::count(opts.room, idx_range) > opts.viewport_size)
				return true;
		}

		const auto ref_id
		{
			m::event_id(ref_idx)
		};

		const m::room ref_room
		{
			opts.room.room_id, ref_id
		};

		const auto &[sound_depth, sound_idx]
		{
			m::sounding(ref_room)
		};

		const auto &[twain_depth, _twain_idx]
		{
			sound_idx == ref_idx?
				m::twain(ref_room):
				std::make_pair(0L, 0UL)
		};

		const auto gap
		{
			sound_depth >= twain_depth?
				size_t(sound_depth - twain_depth):
				0UL
		};

		// Ignore if this ref borders on a gap which does not satisfy the options
		if(gap < opts.gap.first || gap > opts.gap.second)
			return true;

		// The depth on each side of a gap is used as a poor heuristic to
		// guesstimate how many events might be missing and how much to
		// request from a remote at once. Due to protocol limitations, this
		// can err in both directions:
		// - It lowballs in situations like #ping:maunium.net where the DAG
		// is wide, causing more rounds of requests to fill a gap.
		// - It's overzealous in cases of secondary/distant references that
		// have nothing to do with a gap preceding the ref.
		//
		// Fortunately in practice the majority of estimates are close enough.
		// XXX /get_missing_events should be considered if there's low
		// confidence in a gap estimate.
		const auto &limit
		{
			std::clamp(gap, 1UL, 48UL)
		};

		const bool submitted
		{
			submit(event_id, opts.hint, false, limit)
		};

		if(submitted)
			log::debug
			{
				log, "Fetch %s miss prev of %s @%lu in %s @%lu sound:%lu twain:%ld fetching:%zu",
				string_view{event_id},
				string_view{ref_id},
				ref_depth,
				string_view{ref_room.room_id},
				std::get<int64_t>(top),
				sound_depth,
				twain_depth,
				fetching.size(),
			};

		ref_top = std::max(ref_top, ref_idx);
		ret |= submitted;
		return true;
	});

	assert(ref_top >= ref_min);
	ref_min = ref_top;
	return ret;
}

void
ircd::m::acquire::acquire_head()
{
	m::room::head::fetch::opts hfopts;
	hfopts.room_id = opts.room.room_id;
	hfopts.top = m::top(opts.room.room_id);
	m::room::head::fetch
	{
		hfopts, [this, &hfopts](const m::event &result)
		{
			// Bail if interrupted
			if(ctx::interruption_requested())
				return false;

			const auto &[top_id, top_depth, top_idx]
			{
				hfopts.top
			};

			return fetch_head(result, top_depth);
		}
	};
}

bool
ircd::m::acquire::fetch_head(const m::event &result,
                             const int64_t &top_depth)
{
	// Bail if the depth is below the window
	if(json::get<"depth"_>(result) < opts.depth.first)
		return false;

	const auto gap
	{
		json::get<"depth"_>(result) - top_depth
	};

	const auto &limit
	{
		std::clamp(gap, 1L, 48L)
	};

	const auto &hint
	{
		json::get<"origin"_>(result)
	};

	const bool submitted
	{
		submit(result.event_id, hint, true, limit)
	};

	if(submitted)
		log::debug
		{
			log, "Fetch %s head from '%s' in %s @%lu fetching:%zu",
			string_view{result.event_id},
			hint,
			string_view{opts.room.room_id},
			top_depth,
			fetching.size(),
		};

	return true;
}

bool
ircd::m::acquire::submit(const m::event::id &event_id,
                         const string_view &hint,
                         const bool &hint_only,
                         const size_t &limit)
{
	const bool ret
	{
		!started(event_id)?
			start(event_id, hint, hint_only, limit):
			false
	};

	if(ret || full())
		while(handle());

	return ret;
}

bool
ircd::m::acquire::start(const m::event::id &event_id,
                        const string_view &hint,
                        const bool &hint_only,
                        const size_t &limit)
try
{
	fetch::opts fopts;
	fopts.op = fetch::op::backfill;
	fopts.room_id = opts.room.room_id;
	fopts.event_id = event_id;
	fopts.backfill_limit = limit;
	fopts.hint = hint;
	fopts.attempt_limit = hint_only;
	fetching.emplace_back(result
	{
		fetch::start(fopts), event_id
	});

	return true;
}
catch(const ctx::interrupted &e)
{
	throw;
}
catch(const std::exception &e)
{
	log::error
	{
		log, "Fetch %s in %s from '%s' :%s",
		string_view{event_id},
		string_view{opts.room.room_id},
		hint?: "<any>"_sv,
		e.what(),
	};

	return false;
}

bool
ircd::m::acquire::started(const event::id &event_id)
const
{
	const auto it
	{
		std::find_if(std::begin(fetching), std::end(fetching), [&event_id]
		(const auto &result)
		{
			return result.event_id == event_id;
		})
	};

	return it != std::end(fetching);
}

bool
ircd::m::acquire::handle()
{
	if(fetching.empty())
		return false;

	auto next
	{
		ctx::when_any(std::begin(fetching), std::end(fetching), []
		(auto &it) -> ctx::future<m::fetch::result> &
		{
			return it->future;
		})
	};

	const milliseconds timeout
	{
		full()? 5000: 50
	};

	if(!next.wait(timeout, std::nothrow))
		return full();

	const unique_iterator it
	{
		fetching, next.get()
	};

	assert(it.it != std::end(fetching));
	return handle(*it.it);
}

bool
ircd::m::acquire::handle(result &result)
try
{
	auto response
	{
		result.future.get()
	};

	const json::object body
	{
		response
	};

	const json::array pdus
	{
		body["pdus"]
	};

	log::debug
	{
		log, "Eval %zu for %s in %s",
		pdus.size(),
		string_view{result.event_id},
		string_view{opts.room.room_id},
	};

	m::vm::opts vmopts;
	vmopts.infolog_accept = true;
	vmopts.warnlog &= ~vm::fault::EXISTS;
	vmopts.notify_servers = false;
	vmopts.phase.set(m::vm::phase::NOTIFY, false);
	vmopts.phase.set(m::vm::phase::FETCH_PREV, false);
	vmopts.phase.set(m::vm::phase::FETCH_STATE, false);
	vmopts.wopts.appendix.set(dbs::appendix::ROOM_HEAD, false);
	ctx::interruption_point();
	m::vm::eval
	{
		pdus, vmopts
	};

	return true;
}
catch(const ctx::interrupted &e)
{
	throw;
}
catch(const std::exception &e)
{
	log::error
	{
		log, "Eval %s in %s :%s",
		string_view{result.event_id},
		string_view{opts.room.room_id},
		e.what(),
	};

	return true;
}

bool
ircd::m::acquire::full()
const noexcept
{
	return fetching.size() >= opts.fetch_width;
}

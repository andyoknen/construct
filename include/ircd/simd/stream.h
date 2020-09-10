// The Construct
//
// Copyright (C) The Construct Developers, Authors & Contributors
// Copyright (C) 2016-2020 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

#pragma once
#define HAVE_IRCD_SIMD_STREAM_H

namespace ircd::simd
{
	/// Transform block_t by pseudo-reference. The closure has an opportunity
	/// to modify the block while it is being streamed from the source to the
	/// destination. The mask indicates which elements of the block are valid
	/// if the input is smaller than the block size. This function returns
	/// a pair of integers which advance the output and input positions of the
	/// streams for the next iteration.
	template<class block_t>
	using transform_prototype = u64x2 (block_t &, block_t mask);

	template<class block_t_u,
	         class block_t,
	         class lambda>
	u64x2 stream(const char *, const u64x2, lambda&&) noexcept;

	template<class block_t_u,
	         class block_t,
	         class lambda>
	u64x2 stream(char *, const char *, const u64x2, lambda&&) noexcept;
}

/// Streaming transform
///
/// This template performs the loop boiler-plate for the developer who can
/// simply supply a conforming closure. Characteristics:
///
/// * byte-aligned (unaligned): the input and output buffers do not have to
/// be aligned and can be any size.
///
/// * full-duplex: the operation involves both input and output and there are
/// separate pointers for progress across the input and output buffers which
/// are incremented independently.
///
/// * variable-stride: progress for each iteration of the loop across the input
/// and output buffers is not fixed; the transform function may advance either
/// pointer zero to sizeof(block_t) bytes each iteration. Due to these
/// characteristics, unaligned bytes may be redundantly loaded or stored and
/// non-temporal features are not used to optimize the operation.
///
/// u64x2 counter lanes = { output_length, input_length }; The argument `max`
/// gives the buffer size in that format. The return value is the consumed
/// bytes (final counter value) in that format.
///
template<class block_t_u,
         class block_t,
         class lambda>
inline ircd::u64x2
ircd::simd::stream(char *const __restrict__ out,
                   const char *const __restrict__ in,
                   const u64x2 max,
                   lambda&& closure)
noexcept
{
	u64x2 count
	{
		0, // output pos
		0, // input pos
	};

	// primary broadband loop
	while(count[1] + sizeof(block_t) <= max[1] && count[0] + sizeof(block_t) <= max[0])
	{
		static const auto mask
		{
			~block_t{0}
		};

		const auto di
		{
			reinterpret_cast<block_t_u *>(out + count[0])
		};

		const auto si
		{
			reinterpret_cast<const block_t_u *>(in + count[1])
		};

		block_t block
		(
			*si
		);

		const auto consume
		{
			closure(block, mask)
		};

		*di = block;
		count += consume;
	}

	// trailing narrowband loop
	while(count[1] < max[1])
	{
		block_t block {0}, mask {0};
		for(size_t i(0); count[1] + i < max[1] && i < sizeof(block_t); ++i)
		{
			block[i] = in[count[1] + i];
			mask[i] = 0xff;
		}

		const auto consume
		{
			closure(block, mask)
		};

		for(size_t i(0); i < consume[0] && count[0] + i < max[0]; ++i)
			out[count[0] + i] = block[i];

		count += consume;
	}

	return count;
}

/// Streaming consumer
///
/// This template performs the loop boiler-plate for the developer who can
/// simply supply a conforming closure. Characteristics:
///
/// * byte-aligned (unaligned): the input buffer does not have to be aligned
/// and can be any size.
///
/// * variable-stride: progress for each iteration of the loop across the input
/// and buffer is not fixed; the transform function may advance the pointer
/// one to sizeof(block_t) bytes each iteration. Due to these characteristics,
/// unaligned bytes may be redundantly loaded and non-temporal features are
/// not used to optimize the operation.
///
/// u64x2 counter lanes = { available_to_user, input_length }; The argument
/// `max` gives the buffer size in that format. The return value is the
/// consumed bytes (final counter value) in that format. The first lane is
/// available to the user; its initial value is max[0] (also unused); it is
/// then accumulated with the first lane of the closure's return value; its
/// final value is returned in [0] of the return value.
///
/// Note that the closure must advance the stream one or more bytes each
/// iteration; a zero value is available for loop control: the loop will
/// break without another iteration.
///
template<class block_t_u,
         class block_t,
         class lambda>
inline ircd::u64x2
ircd::simd::stream(const char *const __restrict__ in,
                   const u64x2 max,
                   lambda&& closure)
noexcept
{
	u64x2 count
	{
		max[0], // preserved for caller
		0,      // input pos
	};

	u64x2 consume
	{
		0,
		-1UL    // non-zero to start loop
	};

	// primary broadband loop
	while(consume[1] && count[1] + sizeof(block_t) <= max[1])
	{
		static const auto mask
		{
			~block_t{0}
		};

		const auto si
		{
			reinterpret_cast<const block_t_u *>(in + count[1])
		};

		const block_t block
		(
			*si
		);

		consume = closure(block, mask);
		count += consume;
	}

	// trailing narrowband loop
	while(consume[1] && count[1] < max[1])
	{
		block_t block {0}, mask {0};
		for(size_t i(0); count[1] + i < max[1] && i < sizeof(block_t); ++i)
		{
			block[i] = in[count[1] + i];
			mask[i] = 0xff;
		}

		consume = closure(block, mask);
		count += consume;
	}

	return count;
}

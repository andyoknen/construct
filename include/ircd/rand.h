// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

#pragma once
#define HAVE_IRCD_RAND_H

/// Some character set dictionaries
namespace ircd::rand::dict
{
	extern const std::string alnum;
	extern const std::string alpha;
	extern const std::string upper;
	extern const std::string lower;
	extern const std::string numeric;
}

/// Tools for randomization
namespace ircd::rand
{
	struct xoshiro256p;

	// Interface state.
	extern std::random_device device;
	extern std::mt19937_64 mt;

	// Random integer
	uint64_t integer() noexcept;
	uint64_t integer(const uint64_t &min, const uint64_t &max) noexcept; // inclusive
	uint64_t integer(xoshiro256p &) noexcept;

	// Random vector
	template<class T> T vector() noexcept = delete;
	template<> u128x1 vector() noexcept;
	template<> u256x1 vector() noexcept;
	template<> u512x1 vector() noexcept;

	// Random fill of buffer
	const_buffer fill(const mutable_buffer &out) noexcept;

	// Random fill of array
	template<class T,
	         size_t S>
	decltype(auto) fill(T (&buf)[S]) noexcept;

	// Random character from dictionary
	char character(const std::string &dict = dict::alnum) noexcept;

	// Random string from dictionary, fills buffer
	string_view string(const mutable_buffer &out, const std::string &dict) noexcept;
}

struct ircd::rand::xoshiro256p
{
	uint64_t s[4];

	xoshiro256p() noexcept;
};

template<class T,
         size_t S>
inline decltype(auto)
ircd::rand::fill(T (&buf)[S])
noexcept
{
	static_assert
	(
		sizeof(buf) == sizeof(T) * S
	);

	const mutable_buffer mb
	{
		reinterpret_cast<char *>(buf), sizeof(buf)
	};

	fill(mb);
	return buf;
}

/// Random character from dictionary
inline char
ircd::rand::character(const std::string &dict)
noexcept
{
	assert(!dict.empty());
	const auto pos
	{
		integer(0, dict.size() - 1)
	};

	assert(pos < dict.size());
	return dict[pos];
}

inline
ircd::rand::xoshiro256p::xoshiro256p()
noexcept
{
	fill(s);
}

inline uint64_t
ircd::rand::integer(xoshiro256p &state)
noexcept
{
	auto &s(state.s);

	const u64
	ret(s[0] + s[3]),
	t(s[1] << 17);

	s[2] ^= s[0];
	s[3] ^= s[1];
	s[1] ^= s[2];
	s[0] ^= s[3];
	s[2] ^= t;

	#if __has_builtin(__builtin_rotateleft64)
		s[3] = __builtin_rotateleft64(s[3], 45);
	#else
		s[3] = (s[3] << 45) | (s[3] >> (64 - 45));
	#endif

	return ret;
}

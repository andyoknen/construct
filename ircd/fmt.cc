/**
 * Copyright (C) 2016 Charybdis Development Team
 * Copyright (C) 2016 Jason Volk <jason@zemos.net>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice is present in all copies.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <ircd/rfc1459_parse.h>
#include <ircd/rfc1459_gen.h>
#include <ircd/fmt.h>

BOOST_FUSION_ADAPT_STRUCT
(
	ircd::fmt::spec,
	( decltype(ircd::fmt::spec::sign), sign )
	( decltype(ircd::fmt::spec::width), width )
	( decltype(ircd::fmt::spec::name), name )
)

namespace ircd {
namespace fmt  {

namespace qi = boost::spirit::qi;
namespace karma = boost::spirit::karma;

using qi::lit;
using qi::char_;
using qi::int_;
using qi::eps;
using qi::raw;
using qi::repeat;
using qi::omit;
using qi::unused_type;

std::map<string_view, specifier *> _specifiers;

bool is_specifier(const string_view &name);
void handle_specifier(char *&out, const size_t &max, const uint &idx, const spec &, const arg &);
template<class generator> bool generate_string(char *&out, const generator &gen, const arg &val);
template<class T, class lambda> bool visit_type(const arg &val, lambda&& closure);

struct parser
:qi::grammar<const char *, fmt::spec>
{
	template<class R = unused_type> using rule = qi::rule<const char *, R>;

	const rule<> specsym           { lit(SPECIFIER)                            ,"format specifier" };
	const rule<> specterm          { lit(SPECIFIER_TERMINATOR)            ,"specifier termination" };
	const rule<string_view> name
	{
		raw[repeat(1,14)[char_("A-Za-z")]]
		,"specifier name"
	};

	rule<fmt::spec> spec;

	parser()
	:parser::base_type{spec}
	{
		static const auto is_valid([]
		(const auto &str, auto &, auto &valid)
		{
			valid = is_specifier(str);
		});

		spec %= specsym >> -char_("+-") >> -int_ >> name[is_valid] >> -specterm;
	}
}
const parser;

struct string_specifier
:specifier
{
	static const std::tuple
	<
		const char *,
		ircd::string_view,
		std::string_view,
		std::string
	>
	types;

	bool operator()(char *&out, const size_t &max, const spec &, const arg &val) const override;
	using specifier::specifier;
}
const string_specifier
{
	"s"
};

decltype(string_specifier::types)
string_specifier::types
{};

struct signed_specifier
:specifier
{
	static const std::tuple
	<
		char,  unsigned char,
		short, unsigned short,
		int,   unsigned int,
		long,  unsigned long
	>
	types;

	bool operator()(char *&out, const size_t &max, const spec &, const arg &val) const override;
	using specifier::specifier;
}
const signed_specifier
{
	{ "d", "ld", "zd" }
};

decltype(signed_specifier::types)
signed_specifier::types
{};

struct unsigned_specifier
:specifier
{
	static const std::tuple
	<
		char,  unsigned char,
		short, unsigned short,
		int,   unsigned int,
		long,  unsigned long
	>
	types;

	bool operator()(char *&out, const size_t &max, const spec &, const arg &val) const override;
	using specifier::specifier;
}
const unsigned_specifier
{
	{ "u", "lu", "zu" }
};

decltype(unsigned_specifier::types)
unsigned_specifier::types
{};

struct float_specifier
:specifier
{
	static const std::tuple
	<
		char,   unsigned char,
		short,  unsigned short,
		int,    unsigned int,
		long,   unsigned long,
		float,  double
	>
	types;

	bool operator()(char *&out, const size_t &max, const spec &, const arg &val) const override;
	using specifier::specifier;
}
const float_specifier
{
	{ "f", "lf" }
};

decltype(float_specifier::types)
float_specifier::types
{};

struct char_specifier
:specifier
{
	bool operator()(char *&out, const size_t &max, const spec &, const arg &val) const override;
	using specifier::specifier;
}
const char_specifier
{
	"c"
};

struct pointer_specifier
:specifier
{
	bool operator()(char *&out, const size_t &max, const spec &, const arg &val) const override;
	using specifier::specifier;
}
const pointer_specifier
{
	"p"
};

} // namespace fmt
} // namespace ircd

using namespace ircd;

fmt::snprintf::snprintf(internal_t,
                        char *const &out,
                        const size_t &max,
                        const char *const &fstr,
                        const va_rtti &v)
try
:fstart{strchr(fstr, SPECIFIER)}
,fstop{fstr}
,fend{fstr + strlen(fstr)}
,obeg{out}
,oend{out + max}
,out{out}
,idx{0}
{
	if(unlikely(!max))
	{
		fstart = nullptr;
		return;
	}

	if(!fstart)
	{
		append(fstr, fend);
		return;
	}

	append(fstr, fstart);

	auto it(begin(v));
	for(size_t i(0); i < v.size(); ++it, i++)
	{
		const auto &ptr(get<0>(*it));
		const auto &type(get<1>(*it));
		argument(std::make_tuple(ptr, std::type_index(*type)));
	}
}
catch(const std::out_of_range &e)
{
	throw invalid_format("Format string requires more than %zu arguments.", v.size());
}

void
fmt::snprintf::argument(const arg &val)
{
	if(finished())
		return;

	fmt::spec spec;
	if(qi::parse(fstart, fend, parser, spec))
		handle_specifier(out, remaining(), idx++, spec, val);

	fstop = fstart;
	if(fstop < fend)
	{
		fstart = strchr(fstart, SPECIFIER);
		append(fstop, fstart?: fend);
	}
	else *out = '\0';
}

void
fmt::snprintf::append(const char *const &begin,
                      const char *const &end)
{
	const size_t len(std::distance(begin, end));
	const size_t &cpsz(std::min(len, size_t(remaining())));
	memcpy(out, begin, cpsz);
	out += cpsz;
	*out = '\0';
}

const decltype(fmt::_specifiers) &
fmt::specifiers()
{
	return _specifiers;
}

fmt::specifier::specifier(const string_view &name)
:specifier{{name}}
{
}

fmt::specifier::specifier(const std::initializer_list<string_view> &names)
:names{names}
{
	for(const auto &name : this->names)
		if(is_specifier(name))
			throw error("Specifier '%s' already registered\n", name);

	for(const auto &name : this->names)
		_specifiers.emplace(name, this);
}

fmt::specifier::~specifier()
noexcept
{
	for(const auto &name : names)
		_specifiers.erase(name);
}

bool
fmt::is_specifier(const string_view &name)
{
	return specifiers().count(name);
}

void
fmt::handle_specifier(char *&out,
                      const size_t &max,
                      const uint &idx,
                      const spec &spec,
                      const arg &val)
try
{
	const auto &type(get<1>(val));
	const auto &handler(*specifiers().at(spec.name));
	if(!handler(out, max, spec, val))
		throw invalid_type("`%s' for format specifier '%s' for argument #%u",
		                   type.name(),
		                   spec.name,
		                   idx);
}
catch(const std::out_of_range &e)
{
	throw invalid_format("Unhandled specifier `%s' for argument #%u in format string",
	                     spec.name,
	                     idx);
}
catch(const illegal &e)
{
	throw illegal("Specifier `%s' for argument #%u: %s",
	              spec.name,
	              idx,
	              e.what());
}

template<class T,
         class lambda>
bool
fmt::visit_type(const arg &val,
                lambda&& closure)
{
	const auto &ptr(get<0>(val));
	const auto &type(get<1>(val));
	return type == typeid(T)? closure(*static_cast<const T *>(ptr)) : false;
}

bool
fmt::pointer_specifier::operator()(char *&out,
                                   const size_t &max,
                                   const spec &,
                                   const arg &val)
const
{
	using karma::ulong_;
	using karma::eps;
	using karma::maxwidth;

	static const auto throw_illegal([]
	{
		throw illegal("Not a pointer");
	});

	struct generator
	:karma::grammar<char *, uintptr_t()>
	{
		karma::rule<char *, uintptr_t()> pointer_hex
		{
			lit("0x") << karma::hex
		};

		generator(): generator::base_type{pointer_hex} {}
	}
	static const generator;

	const auto &ptr(get<0>(val));
	const auto &type(get<1>(val));
	const void *const p(*static_cast<const void *const *>(ptr));
	return karma::generate(out, maxwidth(max)[generator] | eps[throw_illegal], uintptr_t(p));
}

bool
fmt::char_specifier::operator()(char *&out,
                                const size_t &max,
                                const spec &,
                                const arg &val)
const
{
	using karma::eps;
	using karma::maxwidth;

	static const auto throw_illegal([]
	{
		throw illegal("Not a printable character");
	});

	struct generator
	:karma::grammar<char *, char()>
	{
		karma::rule<char *, char()> printable
		{
			karma::print
			,"character"
		};

		generator(): generator::base_type{printable} {}
	}
	static const generator;

	const auto &ptr(get<0>(val));
	const auto &type(get<1>(val));
	if(type == typeid(const char))
	{
		const auto &c(*static_cast<const char *>(ptr));
		karma::generate(out, maxwidth(max)[generator] | eps[throw_illegal], c);
		return true;
	}
	else return false;
}

bool
fmt::signed_specifier::operator()(char *&out,
                                  const size_t &max,
                                  const spec &s,
                                  const arg &val)
const
{
	static const auto throw_illegal([]
	{
		throw illegal("Failed to print signed value");
	});

	const auto closure([&](const long &integer)
	{
		using karma::long_;
		using karma::maxwidth;

		struct generator
		:karma::grammar<char *, long()>
		{
			karma::rule<char *, long()> rule
			{
				long_
				,"signed long integer"
			};

			generator(): generator::base_type{rule} {}
		}
		static const generator;

		return karma::generate(out, maxwidth(max)[generator] | eps[throw_illegal], integer);
	});

	return !until(types, [&](auto type)
	{
		return !visit_type<decltype(type)>(val, closure);
	});
}

bool
fmt::unsigned_specifier::operator()(char *&out,
                                    const size_t &max,
                                    const spec &s,
                                    const arg &val)
const
{
	static const auto throw_illegal([]
	{
		throw illegal("Failed to print unsigned value");
	});

	const auto closure([&](const ulong &integer)
	{
		using karma::ulong_;
		using karma::maxwidth;

		struct generator
		:karma::grammar<char *, ulong()>
		{
			karma::rule<char *, ulong()> rule
			{
				ulong_
				,"unsigned long integer"
			};

			generator(): generator::base_type{rule} {}
		}
		static const generator;

		return karma::generate(out, maxwidth(max)[generator] | eps[throw_illegal], integer);
	});

	return !until(types, [&](auto type)
	{
		return !visit_type<decltype(type)>(val, closure);
	});
}

bool
fmt::float_specifier::operator()(char *&out,
                                 const size_t &max,
                                 const spec &s,
                                 const arg &val)
const
{
	static const auto throw_illegal([]
	{
		throw illegal("Failed to print floating point value");
	});

	const auto closure([&](const double &floating)
	{
		using karma::double_;
		using karma::maxwidth;

		struct generator
		:karma::grammar<char *, double()>
		{
			karma::rule<char *, double()> rule
			{
				double_
				,"floating point integer"
			};

			generator(): generator::base_type{rule} {}
		}
		static const generator;

		return karma::generate(out, maxwidth(max)[generator] | eps[throw_illegal], floating);
	});

	return !until(types, [&](auto type)
	{
		return !visit_type<decltype(type)>(val, closure);
	});
}

/*
	if(type == typeid(const char[]))
	{
		const auto &i(reinterpret_cast<const char *>(ptr));
		if(!try_lex_cast<ssize_t>(i))
			throw illegal("The string literal value for integer specifier is not a valid integer");

		const auto len(std::min(max, strlen(i)));
		memcpy(out, i, len);
		out += len;
		return true;
	}

	if(type == typeid(const char *))
	{
		const auto &i(*reinterpret_cast<const char *const *>(ptr));
		if(!try_lex_cast<ssize_t>(i))
			throw illegal("The character buffer for integer specifier is not a valid integer");

		const auto len(std::min(max, strlen(i)));
		memcpy(out, i, len);
		out += len;
		return true;
	}

	if(type == typeid(const std::string))
	{
		const auto &i(*reinterpret_cast<const std::string *>(ptr));
		if(!try_lex_cast<ssize_t>(i))
			throw illegal("The string argument for integer specifier is not a valid integer");

		const auto len(std::min(max, i.size()));
		memcpy(out, i.data(), len);
		out += len;
		return true;
	}

	if(type == typeid(const string_view) || type == typeid(const std::string_view))
	{
		const auto &i(*reinterpret_cast<const std::string_view *>(ptr));
		if(!try_lex_cast<ssize_t>(i))
			throw illegal("The string argument for integer specifier is not a valid integer");

		const auto len(std::min(max, i.size()));
		memcpy(out, i.data(), len);
		out += len;
		return true;
	}
*/

bool
fmt::string_specifier::operator()(char *&out,
                                  const size_t &max,
                                  const spec &,
                                  const arg &val)
const
{
	using karma::char_;
	using karma::eps;
	using karma::maxwidth;
	using karma::unused_type;

	static const auto throw_illegal([]
	{
		throw illegal("Not a printable string");
	});

	struct generator
	:karma::grammar<char *, const string_view &>
	{
		karma::rule<char *, const string_view &> string
		{
			*(karma::print)
			,"string"
		};

		generator() :generator::base_type{string} {}
	}
	static const generator;

	return generate_string(out, maxwidth(max)[generator] | eps[throw_illegal], val);
}

template<class generator>
bool
fmt::generate_string(char *&out,
                     const generator &gen,
                     const arg &val)
{
	using karma::eps;

	const auto &ptr(get<0>(val));
	const auto &type(get<1>(val));
	if(type == typeid(ircd::string_view))
	{
		const auto &str(*static_cast<const ircd::string_view *>(ptr));
		return karma::generate(out, gen, str);
	}
	else if(type == typeid(std::string_view))
	{
		const auto &str(*static_cast<const std::string_view *>(ptr));
		return karma::generate(out, gen, str);
	}
	else if(type == typeid(std::string))
	{
		const auto &str(*static_cast<const std::string *>(ptr));
		return karma::generate(out, gen, string_view{str});
	}
	else if(type == typeid(const char *))
	{
		const char *const &str{*static_cast<const char *const *const>(ptr)};
		return karma::generate(out, gen, string_view{str});
	}
	else if(type == typeid(std::exception))
	{
		const auto &e{*static_cast<const std::exception *>(ptr)};
		return karma::generate(out, gen, string_view{e.what()});
	}

	// This for string literals which have unique array types depending on their size.
	// There is no reasonable way to match them. The best that can be hoped for is the
	// grammar will fail gracefully (most of the time) or not print something bogus when
	// it happens to be legal.
	const auto &str(static_cast<const char *>(ptr));
	return karma::generate(out, gen, string_view{str});
}

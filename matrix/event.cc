// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

namespace ircd::m
{
	static json::object make_hashes(const mutable_buffer &out, const sha256::buf &hash);
}

/// The maximum size of an event we will create. This may also be used in
/// some contexts for what we will accept, but the protocol limit and hard
/// worst-case buffer size is still event::MAX_SIZE.
decltype(ircd::m::event::max_size)
ircd::m::event::max_size
{
	{ "name",     "m.event.max_size" },
	{ "default",   65507L            },
};

ircd::json::object
ircd::m::hashes(const mutable_buffer &out,
                const event &event)
{
	const sha256::buf hash_
	{
		hash(event)
	};

	return make_hashes(out, hash_);
}

ircd::json::object
ircd::m::event::hashes(const mutable_buffer &out,
                       json::iov &event,
                       const string_view &content)
{
	const sha256::buf hash_
	{
		hash(event, content)
	};

	return make_hashes(out, hash_);
}

ircd::json::object
ircd::m::make_hashes(const mutable_buffer &out,
                     const sha256::buf &hash)
{
	static const auto b64bufsz(b64encode_size(sizeof(hash)));
	thread_local char hashb64buf[b64bufsz];
	const json::members hashes
	{
		{ "sha256", b64encode_unpadded(hashb64buf, hash) }
	};

	return json::stringify(mutable_buffer{out}, hashes);
}

ircd::sha256::buf
ircd::m::event::hash(const json::object &event)
try
{
	static const size_t iov_max{json::iov::max_size};
	thread_local std::array<json::object::member, iov_max> member;

	size_t i(0);
	for(const auto &m : event)
	{
		if(m.first == "signatures" ||
		   m.first == "hashes" ||
		   m.first == "unsigned" ||
		   m.first == "age_ts" ||
		   m.first == "outlier" ||
		   m.first == "destinations")
			continue;

		member.at(i++) = m;
	}

	thread_local char buf[event::MAX_SIZE];
	const string_view reimage
	{
		json::stringify(buf, member.data(), member.data() + i)
	};

	return sha256{reimage};
}
catch(const std::out_of_range &e)
{
	throw m::BAD_JSON
	{
		"Object has more than %zu member properties.",
		json::iov::max_size
	};
}

ircd::sha256::buf
ircd::m::event::hash(json::iov &event,
                     const string_view &content)
{
	const json::iov::push _content
	{
		event, { "content", content }
	};

	return m::hash(m::event{event});
}

ircd::sha256::buf
ircd::m::hash(const event &event)
{
	if(event.source)
		return event::hash(event.source);

	m::event event_{event};
	json::get<"signatures"_>(event_) = {};
	json::get<"hashes"_>(event_) = {};

	thread_local char buf[event::MAX_SIZE];
	const string_view preimage
	{
		stringify(buf, event_)
	};

	return sha256{preimage};
}

bool
ircd::m::verify_hash(const event &event)
{
	const sha256::buf hash
	{
		m::hash(event)
	};

	return verify_hash(event, hash);
}

bool
ircd::m::verify_hash(const event &event,
                     const sha256::buf &hash)
{
	static constexpr size_t hashb64sz
	{
		size_t(sha256::digest_size * 1.34) + 1
	};

	thread_local char b64buf[hashb64sz];
	return verify_sha256b64(event, b64encode_unpadded(b64buf, hash));
}

bool
ircd::m::verify_sha256b64(const event &event,
                          const string_view &b64)
try
{
	const json::object &object
	{
		at<"hashes"_>(event)
	};

	const json::string &hash
	{
		object.at("sha256")
	};

	return hash == b64;
}
catch(const json::not_found &)
{
	return false;
}

ircd::json::object
ircd::m::event::signatures(const mutable_buffer &out,
                           json::iov &event,
                           const json::iov &content)
{
	const ed25519::sig sig
	{
		sign(event, content)
	};

	thread_local char sigb64buf[b64encode_size(sizeof(sig))];
	const json::members sigb64
	{
		{ self::public_key_id, b64encode_unpadded(sigb64buf, sig) }
	};

	const json::members sigs
	{
		{ event.at("origin"), sigb64 }
    };

	return json::stringify(mutable_buffer{out}, sigs);
}

ircd::m::event
ircd::m::signatures(const mutable_buffer &out_,
                    const m::event &event_)
{
	thread_local char content[event::MAX_SIZE];
	m::event event
	{
		essential(event_, content)
	};

	thread_local char buf[event::MAX_SIZE];
	const json::object &preimage
	{
		stringify(buf, event)
	};

	const ed25519::sig sig
	{
		sign(preimage)
	};

	const auto sig_host
	{
		my_host(json::get<"origin"_>(event))?
			json::get<"origin"_>(event):
			my_host()
	};

	thread_local char sigb64buf[b64encode_size(sizeof(sig))];
	const json::member my_sig
	{
		sig_host, json::members
		{
			{ self::public_key_id, b64encode_unpadded(sigb64buf, sig) }
		}
	};

	static const size_t SIG_MAX{64};
	thread_local std::array<json::member, SIG_MAX> sigs;

	size_t i(0);
	sigs.at(i++) = my_sig;
	for(const auto &[host, sig] : json::get<"signatures"_>(event_))
		if(!my_host(json::string(host)))
			sigs.at(i++) = { host, sig };

	event = event_;
	mutable_buffer out{out_};
	json::get<"signatures"_>(event) = json::stringify(out, sigs.data(), sigs.data() + i);
	return event;
}

ircd::ed25519::sig
ircd::m::event::sign(json::iov &event,
                     const json::iov &contents)
{
	return sign(event, contents, self::secret_key);
}

ircd::ed25519::sig
ircd::m::event::sign(json::iov &event,
                     const json::iov &contents,
                     const ed25519::sk &sk)
{
	ed25519::sig sig;
	essential(event, contents, [&sk, &sig]
	(json::iov &event)
	{
		sig = m::sign(m::event{event}, sk);
	});

	return sig;
}

ircd::ed25519::sig
ircd::m::sign(const event &event)
{
	return sign(event, self::secret_key);
}

ircd::ed25519::sig
ircd::m::sign(const event &event,
              const ed25519::sk &sk)
{
	thread_local char buf[event::MAX_SIZE];
	const string_view preimage
	{
		stringify(buf, event)
	};

	return event::sign(preimage, sk);
}

ircd::ed25519::sig
ircd::m::event::sign(const json::object &event)
{
	return sign(event, self::secret_key);
}

ircd::ed25519::sig
ircd::m::event::sign(const json::object &event,
                     const ed25519::sk &sk)
{
	//TODO: skip rewrite
	thread_local char buf[event::MAX_SIZE];
	const string_view preimage
	{
		stringify(buf, event)
	};

	return sign(preimage, sk);
}

ircd::ed25519::sig
ircd::m::event::sign(const string_view &event)
{
	return sign(event, self::secret_key);
}

ircd::ed25519::sig
ircd::m::event::sign(const string_view &event,
                     const ed25519::sk &sk)
{
	const ed25519::sig sig
	{
		sk.sign(event)
	};

	return sig;
}
bool
ircd::m::verify(const event &event)
{
	const string_view &origin
	{
		at<"origin"_>(event)
	};

	return verify(event, origin);
}

bool
ircd::m::verify(const event &event,
                const string_view &origin)
{
	const json::object &signatures
	{
		at<"signatures"_>(event)
	};

	const json::object &origin_sigs
	{
		signatures.at(origin)
	};

	for(const auto &[host, sig] : origin_sigs)
		if(verify(event, origin, json::string(host)))
			return true;

	return false;
}

bool
ircd::m::verify(const event &event,
                const string_view &origin,
                const string_view &keyid)
try
{
	const m::node node
	{
		origin
	};

	bool ret{false};
	node.key(keyid, [&ret, &event, &origin, &keyid]
	(const ed25519::pk &pk)
	{
		ret = verify(event, pk, origin, keyid);
	});

	return ret;
}
catch(const m::NOT_FOUND &e)
{
	log::derror
	{
		"Failed to verify %s because key %s for %s :%s",
		string_view{event.event_id},
		keyid,
		origin,
		e.what()
	};

	return false;
}

bool
ircd::m::verify(const event &event,
                const ed25519::pk &pk,
                const string_view &origin,
                const string_view &keyid)
{
	const json::object &signatures
	{
		at<"signatures"_>(event)
	};

	const json::object &origin_sigs
	{
		signatures.at(origin)
	};

	const ed25519::sig sig
	{
		[&origin_sigs, &keyid](auto &buf)
		{
			b64decode(buf, json::string(origin_sigs.at(keyid)));
		}
	};

	return verify(event, pk, sig);
}

bool
ircd::m::verify(const event &event_,
                const ed25519::pk &pk,
                const ed25519::sig &sig)
{
	thread_local char buf[2][event::MAX_SIZE];
	m::event event
	{
		essential(event_, buf[0])
	};

	const json::object &preimage
	{
		stringify(buf[1], event)
	};

	return pk.verify(preimage, sig);
}

bool
ircd::m::event::verify(const json::object &event,
                       const ed25519::pk &pk,
                       const ed25519::sig &sig)
{
	thread_local char buf[event::MAX_SIZE];
	const string_view preimage
	{
		stringify(buf, event)
	};

	return pk.verify(preimage, sig);
}

void
ircd::m::event::essential(json::iov &event,
                          const json::iov &contents,
                          const event::closure_iov_mutable &closure)
try
{
	const auto &type
	{
		event.at("type")
	};

	if(type == "m.room.aliases")
	{
		const json::iov::push _content{event,
		{
			"content", json::members
			{
				{ "aliases", contents.at("aliases") }
			}
		}};

		closure(event);
	}
	else if(type == "m.room.create")
	{
		const json::iov::push _content{event,
		{
			"content", json::members
			{
				{ "creator", contents.at("creator") }
			}
		}};

		closure(event);
	}
	else if(type == "m.room.history_visibility")
	{
		const json::iov::push _content{event,
		{
			"content", json::members
			{
				{ "history_visibility", contents.at("history_visibility") }
			}
		}};

		closure(event);
	}
	else if(type == "m.room.join_rules")
	{
		const json::iov::push _content{event,
		{
			"content", json::members
			{
				{ "join_rule", contents.at("join_rule") }
			}
		}};

		closure(event);
	}
	else if(type == "m.room.member")
	{
		const json::iov::push _content{event,
		{
			"content", json::members
			{
				{ "membership", contents.at("membership") }
			}
		}};

		closure(event);
	}
	else if(type == "m.room.power_levels")
	{
		const json::iov::push _content{event,
		{
			"content", json::members
			{
				{ "ban", contents.at("ban")                        },
				{ "events", contents.at("events")                  },
				{ "events_default", contents.at("events_default")  },
				{ "kick", contents.at("kick")                      },
				{ "redact", contents.at("redact")                  },
				{ "state_default", contents.at("state_default")    },
				{ "users", contents.at("users")                    },
				{ "users_default", contents.at("users_default")    },
			}
		}};

		closure(event);
	}
	else if(type == "m.room.redaction")
	{
		// This simply finds the redacts key and swaps it with jsundefined for
		// the scope's duration. The redacts key will still be present and
		// visible in the json::iov which is incorrect if directly serialized.
		// However, this iov is turned into a json::tuple (m::event) which ends
		// up being serialized for signing. That serialization is where the
		// jsundefined redacts value is ignored.
		auto &redacts{event.at("redacts")};
		json::value temp(std::move(redacts));
		redacts = json::value{};
		const unwind _{[&redacts, &temp]
		{
			redacts = std::move(temp);
		}};

		const json::iov::push _content
		{
			event, { "content", "{}" }
		};

		closure(event);
	}
	else
	{
		const json::iov::push _content
		{
			event, { "content", "{}" }
		};

		closure(event);
	}
}
catch(const std::exception &e)
{
	log::derror
	{
		log, "Error while isolating essential keys (redaction algorithm) :%s",
		e.what(),
	};

	throw;
}

ircd::m::event
ircd::m::essential(m::event event,
                   const mutable_buffer &contentbuf)
try
{
	const auto &type
	{
		json::at<"type"_>(event)
	};

	json::object &content
	{
		json::get<"content"_>(event)
	};

	mutable_buffer essential
	{
		contentbuf
	};

	if(type == "m.room.aliases")
	{
		if(content.has("aliases"))
			content = json::stringify(essential, json::members
			{
				{ "aliases", content.at("aliases") }
			});
	}
	else if(type == "m.room.create")
	{
		if(content.has("creator"))
			content = json::stringify(essential, json::members
			{
				{ "creator", content.at("creator") }
			});
	}
	else if(type == "m.room.history_visibility")
	{
		if(content.has("history_visibility"))
			content = json::stringify(essential, json::members
			{
				{ "history_visibility", content.at("history_visibility") }
			});
	}
	else if(type == "m.room.join_rules")
	{
		if(content.has("join_rule"))
			content = json::stringify(essential, json::members
			{
				{ "join_rule", content.at("join_rule") }
			});
	}
	else if(type == "m.room.member")
	{
		if(content.has("membership"))
			content = json::stringify(essential, json::members
			{
				{ "membership", content.at("membership") }
			});
	}
	else if(type == "m.room.power_levels")
	{
		json::stack out{essential};
		json::stack::object top{out};

		if(content.has("ban"))
			json::stack::member
			{
				top, "ban", content.at("ban")
			};

		if(content.has("events"))
			json::stack::member
			{
				top, "events", content.at("events")
			};

		if(content.has("events_default"))
			json::stack::member
			{
				top, "events_default", content.at("events_default")
			};

		if(content.has("kick"))
			json::stack::member
			{
				top, "kick", content.at("kick")
			};

		if(content.has("redact"))
			json::stack::member
			{
				top, "redact", content.at("redact")
			};

		if(content.has("state_default"))
			json::stack::member
			{
				top, "state_default", content.at("state_default")
			};

		if(content.has("users"))
			json::stack::member
			{
				top, "users", content.at("users")
			};

		if(content.has("users_default"))
			json::stack::member
			{
				top, "users_default", content.at("users_default")
			};

		top.~object();
		content = out.completed();
	}
	else if(type == "m.room.redaction")
	{
		json::get<"redacts"_>(event) = string_view{};
		content = "{}"_sv;
	}
	else
	{
		content = "{}"_sv;
	}

	json::get<"signatures"_>(event) = {};
	return event;
}
catch(const std::exception &e)
{
	log::derror
	{
		log, "Error while isolating essential keys (redaction algorithm) :%s",
		e.what(),
	};

	throw;
}

ircd::m::id::event
ircd::m::make_id(const event &event,
                 const string_view &version,
                 id::event::buf &buf)
{
	if(version == "1" || version == "2")
	{
		const crh::sha256::buf hash{event};
		return make_id(event, version, buf, hash);
	}

	if(version == "3")
		return event::id::v3
		{
			buf, event
		};

	return event::id::v4
	{
		buf, event
	};
}

ircd::m::id::event
ircd::m::make_id(const event &event,
                 const string_view &version,
                 id::event::buf &buf,
                 const const_buffer &hash)
{
	char readable[b64encode_size(sha256::digest_size)];

	if(version == "1" || version == "2")
	{
		const id::event ret
		{
			buf, b64tob64url(readable, b64encode_unpadded(readable, hash)), my_host()
		};

		buf.assigned(ret);
		return ret;
	}
	else if(version == "3")
	{
		const id::event ret
		{
			buf, b64encode_unpadded(readable, hash), string_view{}
		};

		buf.assigned(ret);
		return ret;
	}

	const id::event ret
	{
		buf, b64tob64url(readable, b64encode_unpadded(readable, hash)), string_view{}
	};

	buf.assigned(ret);
	return ret;
}

bool
ircd::m::check_id(const event &event)
noexcept
{
	if(!event.event_id)
		return false;

	const string_view &version
	{
		event.event_id.version()
	};

	return check_id(event, version);
}

bool
ircd::m::check_id(const event &event,
                  const string_view &room_version)
noexcept try
{
	assert(event.event_id);
	const auto &version
	{
		room_version?: event.event_id.version()
	};

	char buf[64];
	const event::id &check_id
	{
		version == "1" || version == "2"?
			event::id{json::get<"event_id"_>(event)}:

		version == "3"?
			event::id{event::id::v3{buf, event}}:

		event::id{event::id::v4{buf, event}}
	};

	return event.event_id == check_id;
}
catch(const std::exception &e)
{
	log::error
	{
		"m::check_id() :%s", e.what()
	};

	return false;
}
catch(...)
{
	assert(0);
	return false;
}

bool
ircd::m::before(const event &a,
                const event &b)
{
	const event::prev prev{b};
	return prev.prev_events_has(a.event_id);
}

size_t
ircd::m::degree(const event &event)
{
	return degree(event::prev{event});
}

size_t
ircd::m::degree(const event::prev &prev)
{
	size_t ret{0};
	json::for_each(prev, [&ret]
	(const auto &, const json::array &prevs)
	{
		ret += prevs.count();
	});

	return ret;
}

bool
ircd::m::operator>=(const event &a, const event &b)
{
	//assert(json::get<"room_id"_>(a) == json::get<"room_id"_>(b));
	return json::get<"depth"_>(a) >= json::get<"depth"_>(b);
}

bool
ircd::m::operator<=(const event &a, const event &b)
{
	//assert(json::get<"room_id"_>(a) == json::get<"room_id"_>(b));
	return json::get<"depth"_>(a) <= json::get<"depth"_>(b);
}

bool
ircd::m::operator>(const event &a, const event &b)
{
	//assert(json::get<"room_id"_>(a) == json::get<"room_id"_>(b));
	return json::get<"depth"_>(a) > json::get<"depth"_>(b);
}

bool
ircd::m::operator<(const event &a, const event &b)
{
	//assert(json::get<"room_id"_>(a) == json::get<"room_id"_>(b));
	return json::get<"depth"_>(a) < json::get<"depth"_>(b);
}

bool
ircd::m::operator==(const event &a, const event &b)
{
	//assert(json::get<"room_id"_>(a) == json::get<"room_id"_>(b));
	return a.event_id == b.event_id;
}

bool
ircd::m::bad(const id::event &event_id)
{
	bool ret {false};
	index(event_id, std::nothrow, [&ret]
	(const event::idx &event_idx)
	{
		ret = event_idx == 0;
	});

	return ret;
}

size_t
ircd::m::count(const event::prev &prev)
{
	size_t ret{0};
	m::for_each(prev, [&ret](const event::id &event_id)
	{
		++ret;
		return true;
	});

	return ret;
}

bool
ircd::m::good(const id::event &event_id)
{
	return bool(event_id) && index(event_id, std::nothrow) != 0;
}

bool
ircd::m::exists(const id::event &event_id,
                const bool &good)
{
	return good?
		m::good(event_id):
		m::exists(event_id);
}

bool
ircd::m::exists(const id::event &event_id)
{
	auto &column
	{
		dbs::event_idx
	};

	return bool(event_id) && has(column, event_id);
}

bool
ircd::m::my(const event &event)
{
	const auto &origin(json::get<"origin"_>(event));
	const auto &sender(json::get<"sender"_>(event));
	const auto &eid(event.event_id);
	return
		origin?
			my_host(origin):
		sender?
			my_host(user::id(sender).host()):
		eid?
			my(event::id(eid)):
		false;
}

bool
ircd::m::my(const id::event &event_id)
{
	assert(event_id.host());
	return self::host(event_id.host());
}

//
// event::event
//

IRCD_MODULE_EXPORT
ircd::m::event::event(const json::members &members)
:super_type
{
	members
}
,event_id
{
	defined(json::get<"event_id"_>(*this))?
		id{json::get<"event_id"_>(*this)}:
		id{},
}
{
}

IRCD_MODULE_EXPORT
ircd::m::event::event(const json::iov &members)
:event
{
	members,
	members.has("event_id")?
		id{members.at("event_id")}:
		id{}
}
{
}

IRCD_MODULE_EXPORT
ircd::m::event::event(const json::iov &members,
                      const id &id)
:super_type
{
	members
}
,event_id
{
	id
}
{
}

IRCD_MODULE_EXPORT
ircd::m::event::event(const json::object &source)
:super_type
{
	source
}
,source
{
	source
}
,event_id
{
	defined(json::get<"event_id"_>(*this))?
		id{json::get<"event_id"_>(*this)}:
		id{},
}
{
}

IRCD_MODULE_EXPORT
ircd::m::event::event(const json::object &source,
                      const keys &keys)
:super_type
{
	source, keys
}
,source
{
	source
}
,event_id
{
	defined(json::get<"event_id"_>(*this))?
		id{json::get<"event_id"_>(*this)}:
		id{},
}
{
}

IRCD_MODULE_EXPORT
ircd::m::event::event(id::buf &buf,
                      const json::object &source,
                      const string_view &version)
:event
{
	source,
	version == "1"?
		id{json::string(source.get("event_id"))}:
	version == "2"?
		id{json::string(source.get("event_id"))}:
	version == "3"?
		id{id::v3{buf, source}}:
	version == "4"?
		id{id::v4{buf, source}}:
	source.has("event_id")?
		id{json::string(source.at("event_id"))}:
		id{id::v4{buf, source}},
}
{
}

IRCD_MODULE_EXPORT
ircd::m::event::event(const json::object &source,
                      const id &event_id)
try
:super_type
{
	source
}
,source
{
	source
}
,event_id
{
	event_id?
		event_id:
	defined(json::get<"event_id"_>(*this))?
		id{json::get<"event_id"_>(*this)}:
		id{},
}
{
}
catch(const json::parse_error &e)
{
	log::error
	{
		log, "Event %s from JSON source (%zu bytes) :%s",
		event_id?
			string_view{event_id}:
			"<event_id in source>"_sv,
		string_view{source}.size(),
		e.what(),
	};
}

IRCD_MODULE_EXPORT
ircd::m::event::event(const json::object &source,
                      const id &event_id,
                      const keys &keys)
try
:super_type
{
	source, keys
}
,source
{
	source
}
,event_id
{
	event_id?
		event_id:
	defined(json::get<"event_id"_>(*this))?
		id{json::get<"event_id"_>(*this)}:
		id{},
}
{
}
catch(const json::parse_error &e)
{
	log::error
	{
		log, "Event %s from JSON source (%zu bytes) keys:%zu :%s",
		string_view{event_id},
		string_view{source}.size(),
		keys.count(),
		e.what(),
	};
}
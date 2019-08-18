// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

using namespace ircd;

static resource::response
post__createroom(client &client,
                 const resource::request::object<m::createroom> &request);

mapi::header
IRCD_MODULE
{
	"Client 7.1.1 :Create Room"
};

resource
createroom_resource
{
	"/_matrix/client/r0/createRoom",
	{
		"(7.1.1) Create a new room with various configuration options."
	}
};

resource::method
post_method
{
	createroom_resource, "POST", post__createroom,
	{
		post_method.REQUIRES_AUTH
	}
};

struct report_error
{
	static thread_local char buf[512];

	template<class... args>
	report_error(json::stack::array *const &errors,
	             const string_view &room_id,
	             const string_view &user_id,
	             const string_view &fmt,
	             args&&... a);
};

const string_view
spec_presets[]
{
	"private_chat",
	"public_chat",
	"trusted_private_chat"
};

static bool
spec_preset(const string_view &preset)
{
	return std::find(begin(spec_presets), end(spec_presets), preset) != end(spec_presets);
}

decltype(ircd::m::createroom::version_default)
IRCD_MODULE_EXPORT_DATA
ircd::m::createroom::version_default
{
	{ "name",     "ircd.m.createroom.version_default"  },
	{ "default",  "5"                                  },
};

resource::response
post__createroom(client &client,
                 const resource::request::object<m::createroom> &request)
{
	m::createroom c
	{
		request
	};

	// unconditionally set/override the room_version here
	json::get<"room_version"_>(c) = string_view
	{
		m::createroom::version_default
	};

	// unconditionally set the creator
	json::get<"creator"_>(c) = request.user_id;

	// unconditionally  generate a room_id
	const m::id::room::buf room_id
	{
		m::id::generate, my_host()
	};

	json::get<"room_id"_>(c) = room_id;

	// conditionally override any preset string that isn't
	// allowed from this client.
	if(!spec_preset(json::get<"preset"_>(c)))
		json::get<"preset"_>(c) = string_view{};

	// the response only contains a room mxid, but we leave some extra buffer
	// to collect any non-fatal error messages accumulated during the process
	const unique_buffer<mutable_buffer> buf
	{
		4_KiB
	};

	json::stack out{buf};
	json::stack::object top
	{
		out
	};

	// note the room_id is already known (and output buffered) before the
	// room creation process has even started
	json::stack::member
	{
		top, "room_id", room_id
	};

	json::stack::array errors
	{
		top, "errors"
	};

	const m::room room
	{
		m::create(c, &errors)
	};

	errors.~array();
	top.~object();
	return resource::response
	{
		client, http::CREATED, json::object
		{
			out.completed()
		}
	};

	return {};
}

namespace ircd::m
{
	static room _create_event(const createroom &);
};

ircd::m::room
IRCD_MODULE_EXPORT
ircd::m::create(const createroom &c,
                json::stack::array *const &errors)
try
{
	const m::user::id &creator
	{
		at<"creator"_>(c)
	};

	// Initial create event is committed here first; note that this means the
	// room is officially created and known to the system when this object
	// constructs. Since this overall process including the rest of this scope
	// is not naturally atomic, we shouldn't throw and abort after this point
	// otherwise the full multi-event creation will not be completed. After
	// this point we should report all errors to the errors array.
	const room room
	{
		_create_event(c)
	};

	const m::room::id &room_id
	{
		room.room_id
	};
	assert(room_id == json::get<"room_id"_>(c));

	const json::string preset
	{
		json::get<"preset"_>(c)
	};

	// creator join event

	// user rooms don't have their user joined to them at this time otherwise
	// they'll appear to clients.
	if(!preset || spec_preset(preset))
	{
		const event::id::buf join_event_id
		{
			join(room, creator)
		};
	}

	// initial power_levels

	// initial power levels aren't set on internal user rooms for now.
	if(!preset || spec_preset(preset)) try
	{
		thread_local char content_buf[8_KiB];
		const json::object content
		{
			// If there is an override, use it
			json::get<"power_level_content_override"_>(c)?
				json::get<"power_level_content_override"_>(c):

			// Otherwise generate the default content which allows our closure
			// to add some items to the collections while it's buffering.
			m::room::power::compose_content(content_buf, [&c, &creator, &preset]
			(const string_view &key, json::stack::object &object)
			{
				if(key != "users")
					return;

				// Give the creator their power in the users collection
				json::stack::member
				{
					object, creator, json::value(room::power::default_creator_level)
				};

				// For trusted_private_chat we need to promote everyone invited
				// to the same level in the users collection
				if(preset != "trusted_private_chat")
					return;

				for(const json::string &user_id : json::get<"invite"_>(c))
					if(valid(id::USER, user_id))
						json::stack::member
						{
							object, user_id, json::value(room::power::default_creator_level)
						};
			})
		};

		send(room, creator, "m.room.power_levels", "", content);
	}
	catch(const std::exception &e)
	{
		report_error
		{
			errors, room_id, creator, "Failed to set power_levels: %s", e.what()
		};
	}

	// initial join_rules

	const string_view &join_rule
	{
		preset == "private_chat"?         "invite":
		preset == "trusted_private_chat"? "invite":
		preset == "public_chat"?          "public":
		                                  "invite"
	};

	if(join_rule != "invite") try
	{
		send(room, creator, "m.room.join_rules", "",
		{
			{ "join_rule", join_rule }
		});
	}
	catch(const std::exception &e)
	{
		report_error
		{
			errors, room_id, creator, "Failed to set join_rules: %s", e.what()
		};
	}

	// initial history_visibility

	const string_view &history_visibility
	{
		preset == "private_chat"?         "shared":
		preset == "trusted_private_chat"? "shared":
		preset == "public_chat"?          "shared":
		                                  "shared"
	};

	if(history_visibility != "shared") try
	{
		send(room, creator, "m.room.history_visibility", "",
		{
			{ "history_visibility", history_visibility }
		});
	}
	catch(const std::exception &e)
	{
		report_error
		{
			errors, room_id, creator, "Failed to set history_visibility: %s", e.what()
		};
	}

	// initial guest_access

	const string_view &guest_access
	{
		preset == "private_chat"?         "forbidden":
		preset == "trusted_private_chat"? "forbidden":
		preset == "public_chat"?          "forbidden":
		                                  "forbidden"
	};

	if(guest_access == "can_join") try
	{
		send(room, creator, "m.room.guest_access", "",
		{
			{ "guest_access", "can_join" }
		});
	}
	catch(const std::exception &e)
	{
		report_error
		{
			errors, room_id, creator, "Failed to set guest_access: %s", e.what()
		};
	}

	// user's initial state vector
	//
	// Takes precedence over events set by preset, but gets overriden by name
	// and topic keys.

	size_t i(0);
	for(const json::object &event : json::get<"initial_state"_>(c)) try
	{
		const json::string &type(event["type"]);
		const json::string &state_key(event["state_key"]);
		const json::object &content(event["content"]);
		send(room, creator, type, state_key, content);
		++i;
	}
	catch(const std::exception &e)
	{
		report_error
		{
			errors, room_id, creator, "Failed to set initial_state event @%zu: %s",
			i++,
			e.what()
		};
	}

	// override room name

	if(json::get<"name"_>(c)) try
	{
		static const size_t name_max_len
		{
			// 14.2.1.3: The name of the room. This MUST NOT exceed 255 bytes.
			255
		};

		const auto name
		{
			trunc(json::get<"name"_>(c), name_max_len)
		};

		send(room, creator, "m.room.name", "",
		{
			{ "name", name }
		});
	}
	catch(const std::exception &e)
	{
		report_error
		{
			errors, room_id, creator, "Failed to set room name: %s", e.what()
		};
	}

	// override topic

	if(json::get<"topic"_>(c)) try
	{
		send(room, creator, "m.room.topic", "",
		{
			{ "topic", json::get<"topic"_>(c) }
		});
	}
	catch(const std::exception &e)
	{
		report_error
		{
			errors, room_id, creator, "Failed to set room topic: %s", e.what()
		};
	}

	// invitation vector

	for(const json::string &_user_id : json::get<"invite"_>(c)) try
	{
		json::iov content;
		const json::iov::add is_direct // Conditionally add is_direct
		{
			content, json::get<"is_direct"_>(c),
			{
				"is_direct", []() -> json::value { return json::literal_true; }
			}
		};

		const m::user::id &user_id{_user_id};
		invite(room, user_id, creator, content);
	}
	catch(const m::error &e)
	{
		report_error
		{
			errors, room_id, creator, "Failed to invite user '%s' :%s :%s :%s",
			_user_id,
			e.what(),
			e.errcode(),
			e.errstr()
		};

		// For DM's if we can't invite the counter-party there's no point in
		// creating the room, we can just abort instead.
		if(json::get<"is_direct"_>(c))
			throw;
	}
	catch(const std::exception &e)
	{
		report_error
		{
			errors, room_id, creator, "Failed to invite user '%s' :%s",
			_user_id,
			e.what()
		};
	}

	// override guest_access

	if(json::get<"guest_can_join"_>(c) && guest_access != "can_join") try
	{
		send(room, creator, "m.room.guest_access", "",
		{
			{ "guest_access", "can_join" }
		});
	}
	catch(const std::exception &e)
	{
		report_error
		{
			errors, room_id, creator, "Failed to set guest_access: %s", e.what()
		};
	}

	// room directory

	if(json::get<"visibility"_>(c) == "public") try
	{
		// This call sends a message to the !public room to list this room in the
		// public rooms list. We set an empty summary for this room because we
		// already have its state on this server;
		m::rooms::summary::set(room.room_id, json::object{});
	}
	catch(const std::exception &e)
	{
		report_error
		{
			errors, room_id, creator, "Failed to set public visibility: %s", e.what()
		};
	}

	return room;
}
catch(const db::not_found &e)
{
	throw m::error
	{
		http::CONFLICT, "M_ROOM_IN_USE", "The desired room name is in use."
	};

	throw m::error
	{
		http::CONFLICT, "M_ROOM_ALIAS_IN_USE", "An alias of the desired room is in use."
	};

	//XXX: clearly a conflict
}

ircd::m::room
ircd::m::_create_event(const createroom &c)
{
	const m::user::id &creator
	{
		at<"creator"_>(c)
	};

	const auto &type
	{
		json::get<"preset"_>(c)
	};

	const auto &parent
	{
		json::get<"parent_room_id"_>(c)
	};

	const json::object &user_content
	{
		json::get<"creation_content"_>(c)
	};

	const size_t user_content_count
	{
		std::min(size(user_content), 16UL) // cap the number of keys
	};

	const room room
	{
		at<"room_id"_>(c)
	};

	json::iov event;
	json::iov content;
	json::iov::push _user_content[user_content_count];
	make_iov(content, _user_content, user_content_count, user_content);
	const json::iov::push push[]
	{
		{ event,     { "room_id",     room.room_id     }},
		{ event,     { "depth",       0L               }},
		{ event,     { "sender",      creator          }},
		{ event,     { "state_key",   ""               }},
		{ event,     { "type",        "m.room.create"  }},
		{ content,   { "creator",     creator          }},
	};

	const json::iov::add _type
	{
		content, !type.empty() && type != "room",
		{
			"type", [&type]() -> json::value
			{
				return type;
			}
		}
	};

	const string_view &room_version
	{
		json::get<"room_version"_>(c)?
			string_view{json::get<"room_version"_>(c)}:
			string_view{m::createroom::version_default}
	};

	const json::iov::push _room_version
	{
		content,
		{
			"room_version", json::value
			{
				room_version, json::STRING
			}
		}
	};

	m::vm::copts opts;
	opts.room_version = room_version;
	opts.verify = false;
	m::vm::eval
	{
		event, content, opts
	};

	return room;
}

decltype(report_error::buf) thread_local
report_error::buf;

template<class... args>
report_error::report_error(json::stack::array *const &errors,
                           const string_view &room_id,
                           const string_view &user_id,
                           const string_view &fmt,
                           args&&... a)
{
	const string_view msg{fmt::sprintf
	{
		buf, fmt, std::forward<args>(a)...
	}};

	log::derror
	{
		m::log, "Error when creating room %s for user %s :%s",
		room_id,
		user_id,
		msg
	};

	if(errors)
		errors->append(msg);
}

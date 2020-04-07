// The Construct
//
// Copyright (C) The Construct Developers, Authors & Contributors
// Copyright (C) 2016-2020 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

namespace ircd::m
{
	static resource::response handle_key_query_get(client &, const resource::request &);
	extern resource::method key_query_get;

	extern resource key_query_resource;
}

ircd::mapi::header
IRCD_MODULE
{
	"Federation 3.3.2 :Querying Keys Through Another Server"
};

decltype(ircd::m::key_query_resource)
ircd::m::key_query_resource
{
	"/_matrix/key/v2/query/",
	{
		"federation 3.3.2",
		resource::DIRECTORY,
	}
};

decltype(ircd::m::key_query_get)
ircd::m::key_query_get
{
	key_query_resource, "GET", handle_key_query_get
};

ircd::m::resource::response
ircd::m::handle_key_query_get(client &client,
                              const m::resource::request &request)
{
	if(request.parv.size() < 1)
		throw m::NEED_MORE_PARAMS
		{
			"serverName path parameter required"
		};

	char server_name_buf[rfc3986::DOMAIN_BUFSIZE];
	const auto server_name
	{
		url::decode(server_name_buf, request.parv[0])
	};

	char key_id_buf[64];
	const auto key_id
	{
		request.parv.size() > 1?
			url::decode(key_id_buf, request.parv[1]):
			string_view{}
	};

	const time_t minimum_valid_until_ts
	{
		request.query.get<time_t>("minimum_valid_until_ts", ircd::time<milliseconds>())
	};

	if(key_id)
	{
		const auto respond{[&client]
		(const json::object &keys)
		{
			resource::response
			{
				client, keys
			};
		}};

		if(!keys::cache::get(server_name, key_id, respond))
			throw m::NOT_FOUND
			{
				"Key '%s' from server '%s' is not cached by this server",
				key_id,
				server_name,
			};

		return {}; // responded from closure
	}

	resource::response::chunked response
	{
		client, http::OK
	};

	json::stack out
	{
		response.buf, response.flusher()
	};

	json::stack::object top{out};
	json::stack::array server_keys
	{
		top, "server_keys"
	};

	keys::cache::for_each(server_name, [&server_keys, &minimum_valid_until_ts]
	(const m::keys &keys)
	{
		// Condition ignored to match synapse behavior.
		if((false) && json::get<"valid_until_ts"_>(keys) < minimum_valid_until_ts)
			return true;

		server_keys.append(keys.source);
		return true;
	});

	return {};
}

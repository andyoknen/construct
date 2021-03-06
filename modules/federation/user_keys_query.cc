// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2019 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

using namespace ircd;

mapi::header
IRCD_MODULE
{
	"Federation 21 :End-to-End Encryption"
};

m::resource
user_keys_query_resource
{
	"/_matrix/federation/v1/user/keys/query",
	{
		"federation user keys query",
	}
};

static void
_query_user_device(client &,
                   const m::resource::request &,
                   const m::user::devices &,
                   const string_view &device_id,
                   json::stack::object &out);

static m::resource::response
post__user_keys_query(client &client,
                      const m::resource::request &request);

m::resource::method
user_keys_query__post
{
	user_keys_query_resource, "POST", post__user_keys_query,
	{
		user_keys_query__post.VERIFY_ORIGIN
	}
};

m::resource::response
post__user_keys_query(client &client,
                      const m::resource::request &request)
{
	const json::object &request_keys
	{
		request.at("device_keys")
	};

	m::resource::response::chunked response
	{
		client, http::OK
	};

	json::stack out
	{
		response.buf, response.flusher()
	};

	json::stack::object top{out};
	json::stack::object response_keys
	{
		top, "device_keys"
	};

	for(const auto &[user_id_, device_ids_] : request_keys)
	{
		const m::user::id &user_id
		{
			user_id_
		};

		const json::array &device_ids
		{
			device_ids_
		};

		const m::user::devices devices
		{
			user_id
		};

		json::stack::object response_keys_user
		{
			response_keys, user_id
		};

		if(empty(device_ids))
			devices.for_each([&client, &request, &devices, &response_keys_user]
			(const auto &event_idx, const string_view &device_id)
			{
				_query_user_device(client, request, devices, device_id, response_keys_user);
				return true;
			});
		else
			for(const json::string &device_id : device_ids)
				_query_user_device(client, request, devices, device_id, response_keys_user);
	}

	return std::move(response);
}

void
_query_user_device(client &client,
                   const m::resource::request &request,
                   const m::user::devices &devices,
                   const string_view &device_id,
                   json::stack::object &out)
{
	if(!devices.has(device_id, "keys"))
		return;

	json::stack::object object
	{
		out, device_id
	};

	devices.get(std::nothrow, device_id, "keys", [&device_id, &object]
	(const auto &event_idx, const json::object &device_keys)
	{
		for(const auto &member : device_keys)
			json::stack::member
			{
				object, member.first, member.second
			};
	});

	devices.get(std::nothrow, device_id, "display_name", [&device_id, &object]
	(const auto &event_idx, const string_view &display_name)
	{
		json::stack::object non_hancock
		{
			object, "unsigned"
		};

		json::stack::member
		{
			non_hancock, "device_display_name", display_name
		};
	});
}

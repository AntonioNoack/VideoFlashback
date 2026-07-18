#include "ScreenCast.hpp"

#include <dbus/dbus.h>

#include <iostream>
#include <cstring>

static constexpr const char* PORTAL =
    "org.freedesktop.portal.Desktop";


static constexpr const char* OBJECT =
    "/org/freedesktop/portal/desktop";

static constexpr const char* REQUEST_INTERFACE =
    "org.freedesktop.portal.Request";

    static std::string get_request_path(DBusMessage* reply);

bool ScreenCast::initialize()
{
    DBusError error;
    dbus_error_init(&error);


    connection =
        dbus_bus_get(
            DBUS_BUS_SESSION,
            &error);


    if (dbus_error_is_set(&error))
    {
        std::cerr
            << error.message
            << "\n";

        dbus_error_free(&error);
        return false;
    }


    if (!connection)
        return false;


    std::cout
    << "Connected to session bus\n";

    std::cout
        << "Using portal "
        << "org.freedesktop.portal.ScreenCast\n";


    return true;
}



ScreenCast::~ScreenCast()
{
    if (connection)
        dbus_connection_unref(connection);
}



uint32_t ScreenCast::start()
{
    if (!create_session())
        return 0;


    if (!select_sources())
        return 0;


    if (!start_session())
        return 0;


    return node_id;
}

bool ScreenCast::create_session()
{
    DBusMessage* msg =
        dbus_message_new_method_call(
            PORTAL,
            OBJECT,
            "org.freedesktop.portal.ScreenCast",
            "CreateSession");


    if (!msg)
        return false;


    DBusMessageIter args;
    dbus_message_iter_init_append(
        msg,
        &args);


    DBusMessageIter options;

    dbus_message_iter_open_container(
        &args,
        DBUS_TYPE_ARRAY,
        "{sv}",
        &options);


    auto add_string_option =
        [&](const char* key, const char* value)
    {
        DBusMessageIter entry;
        DBusMessageIter variant;

        dbus_message_iter_open_container(
            &options,
            DBUS_TYPE_DICT_ENTRY,
            nullptr,
            &entry);


        dbus_message_iter_append_basic(
            &entry,
            DBUS_TYPE_STRING,
            &key);


        dbus_message_iter_open_container(
            &entry,
            DBUS_TYPE_VARIANT,
            "s",
            &variant);


        dbus_message_iter_append_basic(
            &variant,
            DBUS_TYPE_STRING,
            &value);


        dbus_message_iter_close_container(
            &entry,
            &variant);

        dbus_message_iter_close_container(
            &options,
            &entry);
    };


    add_string_option(
        "session_handle_token",
        "videoflashback_session");


    add_string_option(
        "handle_token",
        "videoflashback_handle");


    dbus_message_iter_close_container(
        &args,
        &options);



    DBusError error;
    dbus_error_init(&error);


    DBusMessage* reply =
        dbus_connection_send_with_reply_and_block(
            connection,
            msg,
            -1,
            &error);


    dbus_message_unref(msg);


    if (dbus_error_is_set(&error))
    {
        std::cerr
            << "CreateSession error: "
            << error.message
            << "\n";

        dbus_error_free(&error);

        return false;
    }


    if (!reply)
    {
        std::cerr
            << "CreateSession returned no reply\n";

        return false;
    }


    std::string request_path =
        get_request_path(reply);


    dbus_message_unref(reply);


    if (request_path.empty())
    {
        std::cerr
            << "No request path\n";

        return false;
    }


    DBusMessage* response = nullptr;


    if (!wait_for_response(
            request_path,
            &response))
    {
        std::cerr
            << "No CreateSession response\n";

        return false;
    }


    DBusMessageIter iter;

    if (!dbus_message_iter_init(
            response,
            &iter))
    {
        dbus_message_unref(response);
        return false;
    }


    uint32_t response_code = 1;

    dbus_message_iter_get_basic(
        &iter,
        &response_code);


    if (response_code != 0)
    {
        std::cerr
            << "CreateSession rejected: "
            << response_code
            << "\n";

        dbus_message_unref(response);
        return false;
    }


    dbus_message_iter_next(&iter);


    DBusMessageIter results;

    dbus_message_iter_recurse(
        &iter,
        &results);


    while (dbus_message_iter_get_arg_type(&results)
           != DBUS_TYPE_INVALID)
    {
        DBusMessageIter entry;

        dbus_message_iter_recurse(
            &results,
            &entry);


        char* key = nullptr;

        dbus_message_iter_get_basic(
            &entry,
            &key);


        if (strcmp(key, "session_handle") == 0)
        {
            dbus_message_iter_next(&entry);


            DBusMessageIter variant;

            dbus_message_iter_recurse(
                &entry,
                &variant);


            char* path = nullptr;

            dbus_message_iter_get_basic(
                &variant,
                &path);


            session_path = path;


            std::cout
                << "Session: "
                << session_path
                << "\n";
        }


        dbus_message_iter_next(&results);
    }


    dbus_message_unref(response);


    return !session_path.empty();
}

bool ScreenCast::wait_for_response(
    const std::string& request_path,
    DBusMessage** response)
{
    while (true)
    {
        DBusMessage* msg =
            dbus_connection_pop_message(
                connection);


        if (!msg)
        {
            dbus_connection_read_write(
                connection,
                100);

            continue;
        }


        if (dbus_message_is_signal(
                msg,
                REQUEST_INTERFACE,
                "Response"))
        {
            const char* path =
                dbus_message_get_path(msg);


            if (path &&
                request_path == path)
            {
                *response = msg;
                return true;
            }
        }


        dbus_message_unref(msg);
    }
}

static std::string get_request_path(
    DBusMessage* reply)
{
    DBusMessageIter iter;

    if (!dbus_message_iter_init(reply, &iter))
        return {};


    if (dbus_message_iter_get_arg_type(&iter)
        != DBUS_TYPE_OBJECT_PATH)
        return {};


    char* path;

    dbus_message_iter_get_basic(
        &iter,
        &path);


    return path;
}

bool ScreenCast::select_sources()
{
    DBusMessage* msg =
        dbus_message_new_method_call(
            "org.freedesktop.portal.Desktop",
            "/org/freedesktop/portal/desktop",
            "org.freedesktop.portal.ScreenCast",
            "SelectSources");


    DBusMessageIter args;
    dbus_message_iter_init_append(
        msg,
        &args);


    // session handle
    const char* session = session_path.c_str();
    dbus_message_iter_append_basic(
        &args,
        DBUS_TYPE_OBJECT_PATH,
        &session);


    DBusMessageIter options;
    dbus_message_iter_open_container(
        &args,
        DBUS_TYPE_ARRAY,
        "{sv}",
        &options);

    dbus_message_iter_close_container(
        &args,
        &options);

    // types = monitor + window
    /*{
        const char* key = "types";
        uint32_t value = 1;

        DBusMessageIter entry;
        DBusMessageIter variant;

        dbus_message_iter_open_container(
            &options,
            DBUS_TYPE_DICT_ENTRY,
            nullptr,
            &entry);


        dbus_message_iter_append_basic(
            &entry,
            DBUS_TYPE_STRING,
            &key);


        dbus_message_iter_open_container(
            &entry,
            DBUS_TYPE_VARIANT,
            "u",
            &variant);


        dbus_message_iter_append_basic(
            &variant,
            DBUS_TYPE_UINT32,
            &value);


        dbus_message_iter_close_container(
            &entry,
            &variant);


        dbus_message_iter_close_container(
            &options,
            &entry);
    }*/



    DBusMessage* reply =
        dbus_connection_send_with_reply_and_block(
            connection,
            msg,
            -1,
            nullptr);


    dbus_message_unref(msg);


    if (!reply)
        return false;


    std::string request =
        get_request_path(reply);


    dbus_message_unref(reply);


    if (request.empty())
        return false;


    DBusMessage* response = nullptr;


    if (!wait_for_response(
            request,
            &response))
        return false;


    std::cout
        << "Source selection completed\n";


    dbus_message_unref(response);


    return true;
}

bool ScreenCast::start_session()
{
    DBusMessage* msg =
        dbus_message_new_method_call(
            "org.freedesktop.portal.Desktop",
            "/org/freedesktop/portal/desktop",
            "org.freedesktop.portal.ScreenCast",
            "Start");


    DBusMessageIter args;

    dbus_message_iter_init_append(
        msg,
        &args);


    const char* parent = "";
    const char* session = session_path.c_str();

    dbus_message_iter_append_basic(
        &args,
        DBUS_TYPE_OBJECT_PATH,
        &session);


    dbus_message_iter_append_basic(
        &args,
        DBUS_TYPE_STRING,
        &parent);



    DBusMessageIter options;

    dbus_message_iter_open_container(
        &args,
        DBUS_TYPE_ARRAY,
        "{sv}",
        &options);

    dbus_message_iter_close_container(
        &args,
        &options);



    DBusMessage* reply =
        dbus_connection_send_with_reply_and_block(
            connection,
            msg,
            -1,
            nullptr);


    dbus_message_unref(msg);


    if (!reply)
        return false;


    std::string request =
        get_request_path(reply);


    dbus_message_unref(reply);



    DBusMessage* response=nullptr;
    wait_for_response(
        request,
        &response);

    if (!parse_start_response(response))
    {
        dbus_message_unref(response);
        return false;
    }

    dbus_message_unref(response);

    std::cout
        << "ScreenCast started\n";

    return true;
}

bool ScreenCast::parse_start_response(
    DBusMessage* response)
{
    DBusMessageIter iter;

    if (!dbus_message_iter_init(
            response,
            &iter))
    {
        return false;
    }


    uint32_t response_code = 1;

    dbus_message_iter_get_basic(
        &iter,
        &response_code);


    if (response_code != 0)
    {
        std::cerr
            << "Start rejected: "
            << response_code
            << "\n";

        return false;
    }


    dbus_message_iter_next(&iter);


    DBusMessageIter results;

    dbus_message_iter_recurse(
        &iter,
        &results);


    while (dbus_message_iter_get_arg_type(&results)
           != DBUS_TYPE_INVALID)
    {
        DBusMessageIter entry;

        dbus_message_iter_recurse(
            &results,
            &entry);


        char* key = nullptr;

        dbus_message_iter_get_basic(
            &entry,
            &key);


        if (strcmp(key, "streams") == 0)
        {
            dbus_message_iter_next(&entry);


            DBusMessageIter variant;

            dbus_message_iter_recurse(
                &entry,
                &variant);


            DBusMessageIter array;

            dbus_message_iter_recurse(
                &variant,
                &array);


            while (dbus_message_iter_get_arg_type(&array)
                   != DBUS_TYPE_INVALID)
            {
                DBusMessageIter stream_struct;

                dbus_message_iter_recurse(
                    &array,
                    &stream_struct);


                uint32_t id = 0;

                dbus_message_iter_get_basic(
                    &stream_struct,
                    &id);


                node_id = id;


                std::cout
                    << "PipeWire node id: "
                    << node_id
                    << "\n";


                return node_id != 0;
            }
        }


        dbus_message_iter_next(&results);
    }


    return false;
}
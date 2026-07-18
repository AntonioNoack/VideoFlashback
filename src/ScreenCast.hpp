#pragma once

#include <cstdint>
#include <string>

struct DBusConnection;
struct DBusMessage;


class ScreenCast
{
public:

    ~ScreenCast();

    bool initialize();

    uint32_t start();


private:

    bool create_session();

    bool select_sources();

    bool start_session();


    bool wait_for_response(
        const std::string& request_path,
        DBusMessage** response);

    bool parse_start_response(
        DBusMessage* response);


private:

    DBusConnection* connection = nullptr;

    std::string session_path;

    uint32_t node_id = 0;
};
#pragma once

struct control_lib;

struct control_api
{
    control_lib* (*create)(size_t argc, char const* const* argv);
    bool (*destroy)(control_lib*);
    bool (*start)(control_lib*);
    bool (*stop)(control_lib*);
    bool (*update)(control_lib*);
};

constexpr const char* kGetAPIName = "get_control_api";
typedef void (*get_api_fn)(control_api*);

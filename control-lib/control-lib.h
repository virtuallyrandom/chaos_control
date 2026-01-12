#pragma once

struct ControlLib;

struct ControlAPI
{
    ControlLib* (*create)(size_t argc, char const* const* argv);
    bool (*destroy)(ControlLib*);
    bool (*start)(ControlLib*);
    bool (*stop)(ControlLib*);
    bool (*update)(ControlLib*);
};

constexpr const char* kGetAPIName = "Get";
typedef void (*GetAPIFn)(ControlAPI*);

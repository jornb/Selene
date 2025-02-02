#pragma once

#include "exception.h"
#include <iostream>
#include <memory>
#include <string>
#include "Registry.h"
#include "Selector.h"
#include <tuple>
#include "util.h"
#include <vector>

namespace sel {
class State {
private:
    lua_State *_l;
    bool _l_owner;
    std::unique_ptr<Registry> _registry;
    std::unique_ptr<ExceptionHandler> _exception_handler;

public:
    State() : State(false) {}
    State(bool should_open_libs) : _l(nullptr), _l_owner(true), _exception_handler(new ExceptionHandler) {
        _l = luaL_newstate();
        if (_l == nullptr) throw 0;
        if (should_open_libs) luaL_openlibs(_l);
        _registry.reset(new Registry(_l));
        HandleExceptionsPrintingToStdOut();
    }
    State(lua_State *l) : _l(l), _l_owner(false), _exception_handler(new ExceptionHandler) {
        _registry.reset(new Registry(_l));
        HandleExceptionsPrintingToStdOut();
    }
    State(const State &other) = delete;
    State &operator=(const State &other) = delete;
    State(State &&other)
        : _l(other._l),
          _l_owner(other._l_owner),
          _registry(std::move(other._registry)) {
        other._l = nullptr;
    }
    State &operator=(State &&other) {
        if (&other == this) return *this;
        _l = other._l;
        _l_owner = other._l_owner;
        _registry = std::move(other._registry);
        other._l = nullptr;
        return *this;
    }
    ~State() {
        if (_l != nullptr && _l_owner) {
            ForceGC();
            lua_close(_l);
        }
        _l = nullptr;
    }

    int Size() const {
        return lua_gettop(_l);
    }

    bool Load(const std::string &file) {
        ResetStackOnScopeExit savedStack(_l);
        int status = luaL_loadfile(_l, file.c_str());
#if LUA_VERSION_NUM >= 502
        auto const lua_ok = LUA_OK;
#else
        auto const lua_ok = 0;
#endif
        if (status != lua_ok) {
            if (status == LUA_ERRSYNTAX) {
                const char *msg = lua_tostring(_l, -1);
                _exception_handler->Handle(status, msg ? msg : file + ": syntax error");
            } else if (status == LUA_ERRFILE) {
                const char *msg = lua_tostring(_l, -1);
                _exception_handler->Handle(status, msg ? msg : file + ": file error");
            }
            return false;
        }

        status = lua_pcall(_l, 0, LUA_MULTRET, 0);
        if(status == lua_ok) {
            return true;
        }

        const char *msg = lua_tostring(_l, -1);
        _exception_handler->Handle(status, msg ? msg : file + ": dofile failed");
        return false;
    }

    void OpenLib(const std::string& modname, lua_CFunction openf) {
        ResetStackOnScopeExit savedStack(_l);
#if LUA_VERSION_NUM >= 502
        luaL_requiref(_l, modname.c_str(), openf, 1);
#else
        lua_pushcfunction(_l, openf);
        lua_pushstring(_l, modname.c_str());
        lua_call(_l, 1, 0);
#endif
    }

    void HandleExceptionsPrintingToStdOut() {
        *_exception_handler = ExceptionHandler([](int, std::string msg, std::exception_ptr){_print(msg);});
    }

    void HandleExceptionsWith(ExceptionHandler::function handler) {
        *_exception_handler = ExceptionHandler(std::move(handler));
    }

public:
    Selector operator[](const std::string &name) {
        return Selector(_l, *_registry, *_exception_handler, name.c_str());
    }

    Selector operator[](const char *name) {
        return Selector(_l, *_registry, *_exception_handler, name);
    }

    bool operator()(const std::string &code) {
        return operator()(code.c_str());
    }

    bool operator()(const char *code) {
        ResetStackOnScopeExit savedStack(_l);
        int status = luaL_dostring(_l, code);
        if(status) {
            _exception_handler->Handle_top_of_stack(status, _l);
            return false;
        }
        return true;
    }
    void ForceGC() {
        lua_gc(_l, LUA_GCCOLLECT, 0);
    }

    void InteractiveDebug() {
        luaL_dostring(_l, "debug.debug()");
    }

    lua_State* LuaState() {
        return _l;
    }

    std::vector<std::string> GlobalNames() {
        std::vector<std::string> globals;

        if (!_l) return globals;

        lua_pushglobaltable(_l);                    // Get global table
        lua_pushnil(_l);                            // put a nil key on stack
        while (lua_next(_l, -2) != 0) {             // key(-1) is replaced by the next key(-1) in table(-2)

            auto type = lua_type(_l, -2);
            if (type == LUA_TSTRING) {  // check if key is a string
                // you may use key.assign(lua_tostring(L,-2));
                globals.push_back(std::string(lua_tostring(_l, -2)));
            }
            else if (type == LUA_TNUMBER) { //or if it is a number
                char buf[64];
                sprintf_s(buf, "%g", lua_tonumber(_l, -2));
                globals.push_back(std::string(buf));
            }
            lua_pop(_l, 1);                     // remove value(-1), now key on top at(-1)
        }
        lua_pop(_l, 1);                         // remove global table(-1)

        return globals;
    }

    friend std::ostream &operator<<(std::ostream &os, const State &state);
};

inline std::ostream &operator<<(std::ostream &os, const State &state) {
    os << "sel::State - " << state._l;
    return os;
}
}

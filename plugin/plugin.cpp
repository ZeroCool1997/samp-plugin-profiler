// Copyright (c) 2011-2013, Zeex
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <list>
#include <map>
#include <sstream>
#include <string>
#include <subhook.h>
#include <amx/amx.h>
#include <amxprof/call_graph_writer_dot.h>
#include <amxprof/debug_info.h>
#include <amxprof/statistics_writer_html.h>
#include <amxprof/statistics_writer_text.h>
#include <amxprof/statistics_writer_json.h>
#include <amxprof/profiler.h>
#include "amxpath.h"
#include "configreader.h"
#include "plugin.h"
#include "pluginversion.h"

typedef void (*logprintf_t)(const char *format, ...);

extern void *pAMXFunctions;

static logprintf_t logprintf;

typedef std::map<AMX*, AMX_DEBUG> AmxToAmxDebugMap;
static AmxToAmxDebugMap old_debug_hooks;

typedef std::map<AMX*, amxprof::Profiler*> AmxToProfilerMap;
static AmxToProfilerMap profilers;

typedef std::map<AMX*, amxprof::DebugInfo*> AmxToDebugInfoMap; 
static AmxToDebugInfoMap debug_infos;

// Plugin settings and their defauls.
namespace cfg {
  bool          profile_gamemode      = false;
  std::string   profile_filterscripts = "";
  std::string   profile_format        = "html";
  bool          call_graph            = false;
  std::string   call_graph_format     = "dot";
}

static void PrintException(const std::exception &e) {
  logprintf("[profiler] Error: %s", e.what());
}

namespace hooks {

SubHook amx_Exec_hook;
SubHook amx_Callback_hook;

int AMXAPI amx_Debug(AMX *amx) {
  amxprof::Profiler *profiler = ::profilers[amx];
  if (profiler) {
    try {
      profiler->DebugHook();
    } catch (const std::exception &e) {
      PrintException(e);
    }
  }

  AmxToAmxDebugMap::const_iterator iterator = old_debug_hooks.find(amx);
  if (iterator != old_debug_hooks.end()) {
    if (iterator->second != 0) {
      return (iterator->second)(amx);
    }
  }

  return AMX_ERR_NONE;
}

int AMXAPI amx_Callback(AMX *amx, cell index, cell *result, cell *params) {
  SubHook::ScopedRemove r(&amx_Callback_hook);
  SubHook::ScopedInstall i(&amx_Exec_hook);

  amxprof::Profiler *profiler = ::profilers[amx];
  if (profiler != 0) {
    try {
      return profiler->CallbackHook(index, result, params);
    } catch (const std::exception &e) {
      PrintException(e);
    }
  }

  return ::amx_Callback(amx, index, result, params);
}

int AMXAPI amx_Exec(AMX *amx, cell *retval, int index) {
  SubHook::ScopedRemove r(&amx_Exec_hook);
  SubHook::ScopedInstall i(&amx_Callback_hook);

  amxprof::Profiler *profiler = ::profilers[amx];
  if (profiler != 0) {
    try {
      return profiler->ExecHook(retval, index);
    } catch (const std::exception &e) {
      PrintException(e);
    }
  }

  return ::amx_Exec(amx, retval, index);
}

} // namespace hooks

static void ToLower(std::string &s) {
  std::transform(s.begin(), s.end(), s.begin(), ::tolower);
}

static std::string GetAmxPath(AMX *amx) {
  // Has to be static to make caching work in AmxUnload().
  static AmxPathFinder finder;
  finder.AddSearchDirectory("gamemodes");
  finder.AddSearchDirectory("filterscripts");
  return finder.FindAmxPath(amx);
}

static std::string ToUnixPath(const std::string &path) {
  std::string fsPath = path;
  std::replace(fsPath.begin(), fsPath.end(), '\\', '/');
  return fsPath;
}

static bool IsGameMode(const std::string &amx_name) {
  return ToUnixPath(amx_name).find("gamemodes/") != std::string::npos;
}

static bool IsFilterScript(const std::string &amx_name) {
  return ToUnixPath(amx_name).find("filterscripts/") != std::string::npos;
}

static bool WantsProfiler(const std::string &amx_name) {
  std::string good_amx_name = ToUnixPath(amx_name);

  if (IsGameMode(good_amx_name)) {
    if (cfg::profile_gamemode) {
      return true;
    }
  } else if (IsFilterScript(good_amx_name)) {
    std::string fs_list = cfg::profile_filterscripts;
    std::stringstream fs_stream(fs_list);
    do {
      std::string fs_name;
      fs_stream >> fs_name;
      if (good_amx_name == "filterscripts/" + fs_name + ".amx"
          || good_amx_name == "filterscripts/" + fs_name) {
        return true;
      }
    } while (!fs_stream.eof());
  }

  return false;
}

template<typename Map, typename Key>
static void DeleteMapEntry(Map &map, const Key &key) {
  typename Map::iterator iterator = map.find(key);
  if (iterator != map.end()) {
    delete iterator->second;
    map.erase(iterator);
  }
}

template<typename Func>
static void *FunctionToVoidPtr(Func func) {
  return (void*)func;
}

static void *AMXAPI amx_Align_stub(void *v) {
  return v;
}

PLUGIN_EXPORT unsigned int PLUGIN_CALL Supports() {
  return SUPPORTS_VERSION | SUPPORTS_AMX_NATIVES;
}

PLUGIN_EXPORT bool PLUGIN_CALL Load(void **ppData) {
  try {
    pAMXFunctions = ppData[PLUGIN_DATA_AMX_EXPORTS];
    logprintf = (logprintf_t) ppData[PLUGIN_DATA_LOGPRINTF];

    void **exports = reinterpret_cast<void**>(pAMXFunctions);

    exports[PLUGIN_AMX_EXPORT_Align16] = FunctionToVoidPtr(amx_Align_stub);
    exports[PLUGIN_AMX_EXPORT_Align32] = FunctionToVoidPtr(amx_Align_stub);
    exports[PLUGIN_AMX_EXPORT_Align64] = FunctionToVoidPtr(amx_Align_stub);

    hooks::amx_Exec_hook.Install(exports[PLUGIN_AMX_EXPORT_Exec],
                                 FunctionToVoidPtr(hooks::amx_Exec));
    hooks::amx_Callback_hook.Install(exports[PLUGIN_AMX_EXPORT_Callback],
                                     FunctionToVoidPtr(hooks::amx_Callback));

    ConfigReader server_cfg("server.cfg");
    server_cfg.GetOption("profile_gamemode", cfg::profile_gamemode);
    server_cfg.GetOption("profile_filterscripts", cfg::profile_filterscripts);
    server_cfg.GetOption("profile_format", cfg::profile_format);
    server_cfg.GetOption("call_graph", cfg::call_graph);
    server_cfg.GetOption("call_graph_format", cfg::call_graph_format);

    logprintf("  Profiler v" PROJECT_VERSION_STRING " is OK.");
  }
  catch (std::exception &e) {
    PrintException(e);
  }

  return true;
}

PLUGIN_EXPORT int PLUGIN_CALL AmxLoad(AMX *amx) {
  try {
    std::string filename = GetAmxPath(amx);

    if (filename.empty()) {
      logprintf("[profiler] Failed to find corresponding .amx file");
      return AMX_ERR_NONE;
    }

    if (!WantsProfiler(filename)) {
      return AMX_ERR_NONE;
    }

    amxprof::DebugInfo *debug_info = 0;

    if (amxprof::HasDebugInfo(amx)) {
      debug_info = new amxprof::DebugInfo(filename);
      if (debug_info->is_loaded()) {
        ::debug_infos[amx] = debug_info;
      } else {
        logprintf("[profiler] Error loading debug info: %s",
                  aux_StrError(debug_info->last_error()));
        delete debug_info;
        debug_info = 0;
      }
    }

    amxprof::Profiler *profiler = new amxprof::Profiler(amx,
                                                                  debug_info);
    profiler->set_call_graph_enabled(cfg::call_graph);

    if (debug_info != 0) {
      logprintf("[profiler] Attached profiler to '%s'", filename.c_str());
    } else {
      logprintf("[profiler] Attached profiler to '%s' (no debug info)",
                filename.c_str());
    }

    ::old_debug_hooks[amx] = amx->debug;
    amx_SetDebugHook(amx, hooks::amx_Debug);

    ::profilers[amx] = profiler;
  }
  catch (const std::exception &e) {
    PrintException(e);
  }

  return AMX_ERR_NONE;
}

PLUGIN_EXPORT int PLUGIN_CALL AmxUnload(AMX *amx) {
  try {
    amxprof::Profiler *profiler = ::profilers[amx];

    if (profiler != 0) {
      std::string amx_path = GetAmxPath(amx);
      std::string amx_name = std::string(amx_path, 0,
                                         amx_path.find_last_of("."));

      ToLower(cfg::profile_format);
      std::string profile_filename = amx_name + "-profile." +
                                     cfg::profile_format;
      std::ofstream profile_stream(profile_filename.c_str());

      if (profile_stream.is_open()) {
        amxprof::StatisticsWriter *writer = 0;

        if (cfg::profile_format == "html") {
          writer = new amxprof::StatisticsWriterHtml;
        } else if (cfg::profile_format == "txt" ||
                   cfg::profile_format == "text") {
          writer = new amxprof::StatisticsWriterText;
        } else if (cfg::profile_format == "json") {
          writer = new amxprof::StatisticsWriterJson;
        } else {
          logprintf("[profiler] Unrecognized profile format '%s'",
                    cfg::profile_format.c_str());
        }

        if (writer != 0) {
          logprintf("[profiler] Writing profile to '%s'",
                    profile_filename.c_str());
          writer->set_stream(&profile_stream);
          writer->set_script_name(amx_path);
          writer->set_print_date(true);
          writer->set_print_run_time(true);
          writer->Write(profiler->stats());
          delete writer;
        }

        profile_stream.close();
      } else {
        logprintf("[profiler]: Error opening file '%s'",
                  profile_filename.c_str());
      }

      if (cfg::call_graph) {
        ToLower(cfg::call_graph_format);
        std::string call_graph_filename = amx_name + "-calls." +
                                          cfg::call_graph_format;
        std::ofstream call_graph_stream(call_graph_filename.c_str());

        if (call_graph_stream.is_open()) {
          amxprof::CallGraphWriterDot *writer = 0;

          if (cfg::call_graph_format == "dot") {
            writer = new amxprof::CallGraphWriterDot;
          } else {
            logprintf("[profiler] Unrecognized call graph format '%s'",
                      cfg::call_graph_format.c_str());
          }

          if (writer != 0) {
            logprintf("[profiler] Writing call graph to '%s'",
                      call_graph_filename.c_str());
            writer->set_stream(&call_graph_stream);
            writer->set_script_name(amx_path);
            writer->set_root_node_name("SA-MP Server");
            writer->Write(profiler->call_graph());
            delete writer;
          }

          call_graph_stream.close();
        } else {
          logprintf("[profiler]: Error opening file '%s'",
                    call_graph_filename.c_str());
        }
      }
    }

    DeleteMapEntry(::profilers, amx);
    DeleteMapEntry(::debug_infos, amx);
  }
  catch (const std::exception &e) {
    PrintException(e);
  }

  return AMX_ERR_NONE;
}

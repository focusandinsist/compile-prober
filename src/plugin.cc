#include "plugin.h"

#include <cstdlib>
#include <options.h>
#include <plugin-version.h>
#include <tree-check.h>
#include <tree-pass.h>
#include <tree.h>

#include <cp/cp-tree.h>

#include "c-family/c-pragma.h"
#include "cpplib.h"
#include <unistd.h>

int plugin_is_GPL_compatible = 1;

namespace compile_prober {

void cb_finish_parse_function(void *gcc_data, void *user_data) {
  tree decl = (tree)gcc_data;
  auto expanded_location = expand_location(decl->decl_minimal.locus);
  auto decl_name = decl_as_string(decl, 0);
  auto parent_decl = DECL_CONTEXT(decl);
  const char *scope_name = nullptr;
  compile_prober::EventCategory scope_type = compile_prober::EventCategory::UNKNOWN;
  if (parent_decl) {
    if (TREE_CODE(parent_decl) != TRANSLATION_UNIT_DECL) {
      scope_name = decl_as_string(parent_decl, 0);
      switch (TREE_CODE(parent_decl)) {
        case NAMESPACE_DECL:
          scope_type = compile_prober::EventCategory::NAMESPACE;
          break;
        case RECORD_TYPE:
          scope_type = compile_prober::EventCategory::STRUCT;
          break;
        case UNION_TYPE:
          scope_type = compile_prober::EventCategory::STRUCT;
          break;
        default:
          fprintf(stderr, "Unkown tree code %d\n", TREE_CODE(parent_decl));
          break;
      }
    }
  }
  end_parse_function(FinishedFunction{
      gcc_data, decl_name, expanded_location.file, scope_name, scope_type});
}

void cb_plugin_finish(void *gcc_data, void *user_data) { write_all_events(); }

void (*old_file_change_cb)(cpp_reader *, const line_map_ordinary *);

// 预处理核心，这是 真正触发 start/end_preprocess_file 的地方,两个参数类型都是gcc libcpp库的
void cb_file_change(cpp_reader *pfile, const line_map_ordinary *new_map) {
  if (new_map) {
    const char *file_name = ORDINARY_MAP_FILE_NAME(new_map);
    if (file_name) {
      switch (new_map->reason) {
        case LC_ENTER: // （进入文件）
          start_preprocess_file(file_name, pfile);
          break;
        case LC_LEAVE: // （离开文件）
          end_preprocess_file();
          break;
        default:
          break;
      }
    }
  }
  if (old_file_change_cb) {
    (*old_file_change_cb)(pfile, new_map);
  }
}

void cb_start_compilation(void *gcc_data, void *user_data) {
  // 1.手动开始“主文件”的预处理,因为：主文件（.cpp）不是通过 #include 进入的，所以必须手动触发一次
  start_preprocess_file(main_input_filename, nullptr);
  // 2.下面三行：Hook libcpp（超级关键），这一段是整个【预处理 tracing】 的灵魂：
  // 本质：替换 GCC 预处理器的 file_change 回调；
  // 原来：GCC 内部：file_change → 默认处理
  // 现在：GCC → cb_file_change → 再调用 old_file_change_cb
  // 相当于：hook + 不破坏原逻辑
  cpp_callbacks *cpp_cbs = cpp_get_callbacks(parse_in);
  old_file_change_cb = cpp_cbs->file_change;
  cpp_cbs->file_change = &cb_file_change;
}

void cb_pass_execution(void *gcc_data, void *user_data) {
  auto pass = (opt_pass *)gcc_data;
  start_opt_pass(pass);
}

void cb_start_parse_function(void *gcc_data, void *user_data) {
  start_parse_function(gcc_data);
}

}  // namespace compile_prober

static const char *PLUGIN_NAME = "compile-prober";

bool setup_output(int argc, plugin_argument *argv) {
  const char *flag_name = "trace";
  const char *dir_flag_name = "trace-dir";
  // TODO: Maybe make the default filename related to the source filename.
  // TODO: Validate we only compile one TU at a time.
  FILE *trace_file = nullptr;
  if (argc == 0) {
    char file_template[] = "/tmp/trace_XXXXXX.json";
    int fd = mkstemps(file_template, 5);
    if (fd == -1) {
      perror("COMPILE-PROBER mkstemps error: ");
      return false;
    }
    trace_file = fdopen(fd, "w");
    if (!trace_file) {
      close(fd);
    }
  } else if (argc == 1 && argv[0].key && argv[0].value &&
             !strcmp(argv[0].key, flag_name)) {
    trace_file = fopen(argv[0].value, "w");
    if (!trace_file) {
      fprintf(stderr, "COMPILE-PROBER error! Couldn't open %s for writing\n",
              argv[0].value);
    }
  } else if (argc == 1 && argv[0].key && argv[0].value &&
             !strcmp(argv[0].key, dir_flag_name)) {
    std::string file_template{argv[0].value};
    file_template += "/trace_XXXXXX.json";
    int fd = mkstemps(file_template.data(), 5);
    if (fd == -1) {
      perror("COMPILE-PROBER mkstemps error: ");
      return false;
    }
    trace_file = fdopen(fd, "w");
    if (!trace_file) {
      close(fd);
    }
  } else {
    fprintf(stderr,
            "COMPILE-PROBER error! Arguments must be -fplugin-arg-%s-%s=FILENAME or "
            "-fplugin-arg-%s-%s=DIRECTORY\n",
            PLUGIN_NAME, flag_name, PLUGIN_NAME, dir_flag_name);
    return false;
  }
  if (trace_file) {
    compile_prober::init_output_file(trace_file);
    return true;
  } else {
    return false;
  }
}

// 这是 GCC 加载插件时调用的函数，是入口，相当于main
int plugin_init(struct plugin_name_args *plugin_info,
                struct plugin_gcc_version *ver) {
  if (!plugin_default_version_check(ver, &gcc_version)) {
    fprintf(stderr, "COMPILE-PROBER error: incompatible GCC plugin version\n");
    return 1;
  }

  static struct plugin_info compile_prober_info = {
      .version = "V1.0", .help = "Compile Prober traces the compilation."};
  compile_prober::COMPILATION_START = compile_prober::clock_t::now();
  if (!setup_output(plugin_info->argc, plugin_info->argv)) {
    return -1;
  }

  // 注册回调（🔥核心） 把 GCC 的事件“挂钩”到你的函数上
  // register_callback(PLUGIN_NAME, GCC 事件, 
  //                   回调函数, &compile_prober_info);
  // 无用？
  register_callback(PLUGIN_NAME, PLUGIN_INFO, 
                    nullptr, &compile_prober_info);
  // 编译开始
  register_callback(PLUGIN_NAME, PLUGIN_START_UNIT,
                    &compile_prober::cb_start_compilation, nullptr);
  // 函数解析开始与结束，用同一个 decl 精确配对
  register_callback(PLUGIN_NAME, PLUGIN_START_PARSE_FUNCTION,
                    &compile_prober::cb_start_parse_function, nullptr);
  register_callback(PLUGIN_NAME, PLUGIN_FINISH_PARSE_FUNCTION,
                    &compile_prober::cb_finish_parse_function, nullptr);
  // 每个优化 pass
  register_callback(PLUGIN_NAME, PLUGIN_PASS_EXECUTION,
                    &compile_prober::cb_pass_execution, nullptr);
  // 编译结束
  register_callback(PLUGIN_NAME, PLUGIN_FINISH, 
                    &compile_prober::cb_plugin_finish, nullptr);

  return 0;
}

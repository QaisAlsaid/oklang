#ifndef OK_MACROS_HPP
#define OK_MACROS_HPP

// TODO(Qais): config and stuff
#if defined(PARANOID)
#define TRACE(...) get_vm_logger().trace(__VA_ARGS__)
#define LOG(...) get_vm_logger().log(__VA_ARGS__)
#define TRACELN(...) get_vm_logger().traceln(__VA_ARGS__)
#define LOGLN(...) get_vm_logger().logln(__VA_ARGS__)
#else
#define TRACE(...)
#define LOG(...)
#define TRACELN(...)
#define LOGLN(...)
#endif
#if defined(ENABLE_WARNINGS)
#define WARN(...) get_vm_logger().warn(__VA_ARGS__)
#define WARNLN(...) get_vm_logger().warnln(__VA_ARGS__)
#else
#define WARN(...)
#define WARNLN(...)
#endif
#define ERROR(...) get_vm_logger().error(__VA_ARGS__)
#define ERRORLN(...) get_vm_logger().errorln(__VA_ARGS__)

#define ENABLE_ASSERT
#if defined(ENABLE_ASSERT)
#define ASSERT(x)                                                                                                      \
  do                                                                                                                   \
  {                                                                                                                    \
    if(!(x))                                                                                                           \
    {                                                                                                                  \
      ERRORLN("assertion failed in {} ({}:{})", __func__, __FILE__, __LINE__);                                         \
      abort();                                                                                                         \
    }                                                                                                                  \
  } while(0)
#else
#define ASSERT(x)
#endif

#if defined(PARANOID)
// #define OK_STRESS_GC
// #define OK_LOG_GC
#endif
#define OK_NOT_GARBAGE_COLLECTED

#endif // OK_MACROS_HPP
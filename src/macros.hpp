#ifndef OK_MACROS_HPP
#define OK_MACROS_HPP

// TODO(Qais): config and stuff
#define TRACE(...) get_logger().trace(__VA_ARGS__)
#define LOG(...) get_logger().log(__VA_ARGS__)
#define WARN(...) get_logger().warn(__VA_ARGS__)
#define ERROR(...) get_logger().error(__VA_ARGS__)
#define TRACELN(...) get_logger().traceln(__VA_ARGS__)
#define LOGLN(...) get_logger().logln(__VA_ARGS__)
#define WARNLN(...) get_logger().warnln(__VA_ARGS__)
#define ERRORLN(...) get_logger().errorln(__VA_ARGS__)

#define ASSERT(x)                                                                                                      \
  do                                                                                                                   \
  {                                                                                                                    \
    if(!(x))                                                                                                           \
    {                                                                                                                  \
      ERRORLN("assertion failed in {} ({}:{})", __func__, __FILE__, __LINE__);                                         \
      abort();                                                                                                         \
    }                                                                                                                  \
  } while(0)

#endif // OK_MACROS_HPP
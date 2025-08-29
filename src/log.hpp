#ifndef OK_LOG_HPP
#define OK_LOG_HPP

#include <format>
#include <print>
#include <string_view>
namespace ok
{
  enum class log_level
  {
    paranoid,
    log,
    warn,
    error,
    off
  };

  inline constexpr std::string_view log_level_to_string(log_level p_level)
  {
    switch(p_level)
    {
    case log_level::paranoid:
      return "paranoid";
    case log_level::log:
      return "log";
    case log_level::warn:
      return "warn";
    case log_level::error:
      return "error";
    case log_level::off:
      return "off";
    }
  }

  class logger
  {
  public:
    logger(log_level level = log_level::log) : m_level(level)
    {
    }

    template <typename... Args>
    void trace(std::format_string<Args...> fmt, Args&&... args) const
    {
      if(m_level < log_level::log)
        std::print(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void log(std::format_string<Args...> fmt, Args&&... args) const
    {
      if(m_level < log_level::warn)
        std::print(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void warn(std::format_string<Args...> fmt, Args&&... args) const
    {
      if(m_level < log_level::error)
        std::print(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void error(std::format_string<Args...> fmt, Args&&... args) const
    {
      if(m_level < log_level::off)
        std::print(stderr, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void traceln(std::format_string<Args...> fmt, Args&&... args) const
    {
      if(m_level < log_level::log)
        std::println(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void logln(std::format_string<Args...> fmt, Args&&... args) const
    {
      if(m_level < log_level::warn)
        std::println(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void warnln(std::format_string<Args...> fmt, Args&&... args) const
    {
      if(m_level < log_level::error)
        std::println(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void errorln(std::format_string<Args...> fmt, Args&&... args) const
    {
      if(m_level < log_level::off)
        std::println(stderr, fmt, std::forward<Args>(args)...);
    }

  private:
    log_level m_level;
  };
} // namespace ok

#endif // OK_LOG_HPP
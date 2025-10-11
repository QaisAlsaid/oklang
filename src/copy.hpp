#ifndef OK_COPY_HPP
#define OK_COPY_HPP

namespace ok
{
  template <typename T>
  struct copy
  {
    explicit constexpr copy(const T& t) : t(t) {};

    const T& t;
  };
} // namespace ok

#endif // OK_COPY_HPP
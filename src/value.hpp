#ifndef OK_VALUE_HPP
#define OK_VALUE_HPP

#include <vector>

namespace ok
{
  struct value_type
  {
    value_type(double v)
    {
      m_value = v;
    }

    void print() const;

    value_type operator+(const value_type& other) const
    {
      return m_value + other.m_value;
    }
    value_type operator-(const value_type& other) const
    {
      return m_value - other.m_value;
    }
    value_type operator*(const value_type& other) const
    {
      return m_value * other.m_value;
    }
    value_type operator/(const value_type& other) const
    {
      return m_value / other.m_value;
    }

    value_type operator-() const
    {
      return -m_value;
    }

    value_type& operator+=(const value_type& other)
    {
      m_value += other.m_value;
      return *this;
    }

    value_type& operator-=(const value_type& other)
    {
      m_value *= other.m_value;
      return *this;
    }

    value_type& operator*=(const value_type& other)
    {
      m_value *= other.m_value;
      return *this;
    }

    value_type& operator/=(const value_type& other)
    {
      m_value /= other.m_value;
      return *this;
    }

  private:
    double m_value;
  };
  using value_array = std::vector<value_type>;
} // namespace ok

#endif // OK_VALUE_HPP
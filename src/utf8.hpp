#ifndef OK_UTF8_HPP
#define OK_UTF8_HPP

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>

// FIXME(Qais): all calls here are nt safe fix this and define failure behavior!
namespace ok::utf8
{
  // advance one utf8 codepoint
  inline const char* advance(const char* p_current)
  {
    if(p_current == nullptr || *p_current == '\0')
      return 0;

    uint8_t c = static_cast<uint8_t>(*p_current);
    if(c < 0x80)
      return p_current + 1; // ascii
    else if((c & 0xe0) == 0xc0)
      return p_current + 2;
    else if((c & 0xf0) == 0xe0)
      return p_current + 3;
    else if((c & 0xf8) == 0xf0)
      return p_current + 4;
    return p_current; // invalid utf8 (shouldnt be reached cuz we are checking the whole source ahead of time)
  }

  // peek n utf8 codepoints ahead
  inline const char* peek(const char* p_current, size_t n)
  {
    const char* temp = p_current;
    for(size_t i = 0; i < n; ++i)
      temp = advance(temp);

    return temp;
  }

  struct validation_error
  {
    enum class leading
    {
      _1,
      _2,
      _3,
      _4,
      invalid
    } leading;
    const uint8_t* location;
  };

  inline std::expected<int, validation_error> validate_codepoint(const uint8_t* p, const uint8_t* end)
  {
    if(*p < 0x80)
    {
      return 1;
    }
    else if((*p & 0xe0) == 0xc0)
    {
      if(p + 1 >= end || (p[1] & 0xc0) != 0x80 || (*p & 0xfe) == 0xc0)
        return std::unexpected{validation_error{validation_error::leading::_2, p}};
      return 2;
    }
    else if((*p & 0xf0) == 0xe0)
    {
      if(p + 2 >= end || (p[1] & 0xc0) != 0x80 || (p[2] & 0xc0) != 0x80)
        return std::unexpected{validation_error{validation_error::leading::_3, p}};
      uint8_t lead = *p;
      if(lead == 0xe0 && (p[1] & 0xe0) == 0x80)
        return std::unexpected{validation_error{validation_error::leading::_3, p}};
      if(lead == 0xed && (p[1] & 0xe0) == 0xa0)
        return std::unexpected{validation_error{validation_error::leading::_3, p}};
      return 3;
    }
    else if((*p & 0xf8) == 0xf0)
    {
      if(p + 3 >= end || (p[1] & 0xc0) != 0x80 || (p[2] & 0xc0) != 0x80 || (p[3] & 0xc0) != 0x80)
        return std::unexpected{validation_error{validation_error::leading::_4, p}};
      uint8_t lead = *p;
      if(lead == 0xf0 && (p[1] & 0xf0) == 0x80)
        return std::unexpected{validation_error{validation_error::leading::_4, p}};
      if(lead > 0xf4 || (lead == 0xf4 && p[1] > 0x8f))
        return std::unexpected{validation_error{validation_error::leading::_4, p}};
      return 4;
    }
    else
    {
      return std::unexpected{validation_error{validation_error::leading::invalid, p}};
    }
  }

  inline std::expected<void, validation_error> validate(const std::string_view str)
  {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(str.data());
    const uint8_t* end = p + str.size();

    while(p < end)
    {
      auto len = validate_codepoint(p, end);
      if(!len)
        return std::unexpected(len.error());
      p += *len;
    }
    return {};
  }

  /*
  inline std::expected<void, validation_error> validate(const std::string_view str)
  {
    const uint8_t* p = (uint8_t*)str.data();
    const uint8_t* end = p + str.size();

    while(p < end)
    {
      if(*p < 0x80) // ascii
      {
        p++;
      }
      else if((*p & 0xe0) == 0xc0) // 2 bytes
      {
        if(p + 1 >= end || (p[1] & 0xc0) != 0x80 || (*p & 0xfe) == 0xc0)
          return std::unexpected{validation_error{validation_error::leading::_2, p}};
        p += 2;
      }
      else if((*p & 0xf0) == 0xe0) // 3 bytes
      {
        if(p + 2 >= end || (p[1] & 0xc0) != 0x80 || (p[2] & 0xc0) != 0x80)
          return std::unexpected{validation_error{validation_error::leading::_3, p}};
        uint8_t lead = *p;
        if(lead == 0xe0 && (p[1] & 0xe0) == 0x80)
          return std::unexpected{validation_error{validation_error::leading::_3, p}};
        if(lead == 0xed && (p[1] & 0xe0) == 0xa0)
          return std::unexpected{validation_error{validation_error::leading::_3, p}};
        p += 3;
      }
      else if((*p & 0xf8) == 0xf0) // 4 bytes
      {
        if(p + 3 >= end || (p[1] & 0xc0) != 0x80 || (p[2] & 0xc0) != 0x80 || (p[3] & 0xc0) != 0x80)
          return std::unexpected{validation_error{validation_error::leading::_4, p}};
        uint8_t lead = *p;
        if(lead == 0xf0 && (p[1] & 0xf0) == 0x80)
          return std::unexpected{validation_error{validation_error::leading::_4, p}};
        if(lead > 0xf4 || (lead == 0xf4 && p[1] > 0x8f))
          return std::unexpected{validation_error{validation_error::leading::_4, p}};
        p += 4;
      }
      else
      {
        return std::unexpected{validation_error{validation_error::leading::invalid, p}};
      }
    }
    return {};
  }
    */
} // namespace ok::utf8

#endif // OK_UTF8_HPP
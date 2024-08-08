// https://github.com/gpakosz/whereami

// in case you want to #include "whereami++.cpp" in a larger compilation unit
#if !defined(WHEREAMIPP_H)
#include <whereami++.h>
#endif

#define WHEREAMI_H
#define WAI_FUNCSPEC
#define WAI_PREFIX(function) function
#include "whereami.c"

namespace whereami {

  whereami_string_t whereami_path_t::dirname() const
  {
    return _path.substr(0, _dirname_length);
  }

  whereami_string_t whereami_path_t::basename() const
  {
    return _path.substr(_dirname_length + 1);
  }

#if defined(WHEREAMI_CXX11)
  whereami_path_t::operator whereami_string_t() &&
  {
    return std::move(_path);
  }

  whereami_path_t::operator whereami_string_t() const &
  {
    return _path;
  }
#else
  whereami_path_t::operator const whereami_string_t&() const
  {
    return _path;
  }
#endif

#if defined(WHEREAMI_CXX11)
  whereami_path_t::whereami_path_t(whereami_string_t&& path, int dirname_length) noexcept
    : _path(std::move(path)), _dirname_length(dirname_length)
  {
  }
#else
  whereami_path_t::whereami_path_t(whereami_string_t& path, int dirname_length)
    : _path(path), _dirname_length(dirname_length)
  {
  }
#endif

#if !defined(WHEREAMI_DISABLE_OSTREAM)
  std::ostream& operator<<(std::ostream& os, const whereami_path_t& path)
  {
    return os << path._path;
  }

#endif
  WAI_FUNCSPEC
  whereami_path_t getExecutablePath()
  {
    whereami_string_t path;
    int dirname_length = -1;

    int length = ::WAI_PREFIX(getExecutablePath)(0, 0, 0);
    if (length != -1)
    {
      path.resize(length);
      ::WAI_PREFIX(getExecutablePath)(&path[0], length, &dirname_length);
    }

#if defined(WHEREAMI_CXX11)
    return whereami_path_t(std::move(path), dirname_length);
#else
    return whereami_path_t(path, dirname_length);
#endif
  }

  WAI_FUNCSPEC
  whereami_path_t getModulePath()
  {
    whereami_string_t path;
    int dirname_length = -1;

    int length = ::WAI_PREFIX(getModulePath)(0, 0, 0);
    if (length != -1)
    {
      path.resize(length);
      ::WAI_PREFIX(getModulePath)(&path[0], length, &dirname_length);
    }

#if defined(WHEREAMI_CXX11)
    return whereami_path_t(std::move(path), dirname_length);
#else
    return whereami_path_t(path, dirname_length);
#endif
  }

} // namespace whereami

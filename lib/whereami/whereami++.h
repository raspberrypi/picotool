// https://github.com/gpakosz/whereami

#ifndef WHEREAMIPP_H
#define WHEREAMIPP_H

#if !defined(WHEREAMI_STRING_T)
#include <string>
typedef std::string whereami_string_t;
#else
typedef WHEREAMI_STRING_T whereami_string_t;
#endif
#if !defined(WHEREAMI_DISABLE_OSTREAM)
#include <ostream>
#endif

#if (defined (__cplusplus) && (__cplusplus > 199711L)) || (defined(_MSC_FULL_VER) && (_MSC_FULL_VER >= 150020706))
  #define WHEREAMI_CXX11
#endif

namespace whereami {

  class whereami_path_t
  {
    public:
#if defined(WHEREAMI_CXX11)
    operator whereami_string_t() &&;
    operator whereami_string_t() const &;
#else
    operator const whereami_string_t&() const;
#endif
    whereami_string_t dirname() const;
    whereami_string_t basename() const;

    private:
    whereami_path_t();

#if defined(WHEREAMI_CXX11)
    whereami_path_t(whereami_string_t&& path, int dirname_length) noexcept;
#else
    whereami_path_t(whereami_string_t& path, int dirname_length);
#endif

    friend whereami_path_t getExecutablePath();
    friend whereami_path_t getModulePath();
#if !defined(WHEREAMI_DISABLE_OSTREAM)
    friend std::ostream& operator<<(std::ostream& os, const whereami_path_t& path);
#endif

    whereami_string_t _path;
    int _dirname_length;
  };

  /**
   * Returns the path to the current executable.
   */
  whereami_path_t getExecutablePath();

  /**
   * Returns the path to the current module.
   */
  whereami_path_t getModulePath();

} // namespace whereami

#endif // #ifndef WHEREAMIPP_H


/* ===================================================================================================
 * utils/include/utils/mailbox/mailbox.hpp
 *
 * Typed C++ wrapper over the latest-wins mailbox: Mailbox<T> owns one
 * Mailbox slot of sizeof(T) bytes. Same contract as mailbox.h (one writer
 * thread, one reader thread, never blocks); this file only adds the type,
 * the ownership, and a compile-time size check.
 *
 * sizeof(T) must be a multiple of 8: checked by static_assert, so a
 * forgotten pad is a compile error instead of a NULL at runtime.
 *
 * No exceptions: if mb_create fails (out of memory, already logged) the
 * mailbox is invalid. Check valid() once after construction. Every other
 * method requires a valid mailbox.
 *
 * Movable, not copyable. A moved-from mailbox is invalid.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_UTILS_MAILBOX_MAILBOX_HPP
#define MTS_UTILS_MAILBOX_MAILBOX_HPP

#include <utils/mailbox/mailbox.h>

#include <utility>
#include <type_traits>

namespace mts
{

template <typename T>
class Mailbox
{
  static_assert(std::is_trivially_copyable_v<T>, "Mailbox<T> copies T word by word: T must be trivially copyable");
  static_assert(sizeof(T) % 8 == 0, "Mailbox<T> stores T as 8-byte words: pad T to a multiple of 8 bytes");

public:
  Mailbox()
    : mb_(mb_create(sizeof(T)))
  {
  }

  ~Mailbox()
  {
    mb_destroy(mb_);
  }

  Mailbox(Mailbox&& other) noexcept
    : mb_(std::exchange(other.mb_, nullptr))
  {
  }

  Mailbox& operator=(Mailbox&& other) noexcept
  {
    if (this != &other)
    {
      mb_destroy(mb_);
      mb_ = std::exchange(other.mb_, nullptr);
    }
    return *this;
  }

  Mailbox(const Mailbox&) = delete;
  Mailbox& operator=(const Mailbox&) = delete;

  // false if creation failed or the mailbox was moved from
  bool valid() const
  {
    return mb_ != nullptr;
  }

  // Writer only. Replaces the current value
  void write(const T& value)
  {
    mb_write(mb_, &value);
  }

  // Reader only. true = new value since the last read, out filled
  bool read(T& out)
  {
    return mb_read(mb_, &out);
  }

private:
  // Global: the C type of the same name, not this class
  ::Mailbox* mb_;
};

} // namespace mts

#endif /* MTS_UTILS_MAILBOX_MAILBOX_HPP */

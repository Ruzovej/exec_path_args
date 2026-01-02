/*
  Copyright 2025 Lukáš Růžička

  This file is part of exec_path_args.

  exec_path_args is free software: you can redistribute it and/or modify it under the
  terms of the GNU Lesser General Public License as published by the Free
  Software Foundation, either version 3 of the License, or (at your option) any
  later version.

  exec_path_args is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
  details.

  You should have received a copy of the GNU Lesser General Public License along
  with exec_path_args. If not, see <https://www.gnu.org/licenses/>.
*/

// due to https://claude.ai/share/458d69e0-9519-4e20-91eb-8c99c7dd9d0a

#pragma once

#include <string>

namespace detail {

// intended for very simple scheme: only 2 processes, one, the "owner"/parent,
// which creates and destroys internal semaphores, and its "child", that only
// uses them, and whose lifetime is strictly within the lifetime of the parent
struct sema_wrap {
  // Both processes must use the same name to connect to each other
  explicit sema_wrap(std::string const &aName, bool const create);
  ~sema_wrap() noexcept;

  // Wait for notification. Returns true if notified, false on timeout.
  [[nodiscard]] bool wait(int const timeout_ms);

  void notify();

private:
  sema_wrap(sema_wrap const &) = delete;
  sema_wrap &operator=(sema_wrap const &) = delete;

  void *sem;
  std::string name;
  bool owns; // responsible for cleanup
};

} // namespace detail

// ips ~ inter-process synchronization
// see its intended usage & limitations in `sema_wrap` above, or in the
// implementation itself
struct ips {
  // Both processes must use the same name to connect to each other
  explicit ips(std::string const &aName, bool const create);
  ~ips() noexcept = default;

  // Wait for notification. Returns true if notified, false on timeout.
  [[nodiscard]] bool wait(int const timeout_ms);

  void notify();

  [[nodiscard]] bool notify_and_wait(int const timeout_ms) {
    notify();
    return wait(timeout_ms);
  }

  [[nodiscard]] bool wait_and_notify(int const timeout_ms) {
    bool const res{wait(timeout_ms)};
    notify();
    return res;
  }

private:
  detail::sema_wrap sema_wait, sema_notify;
};

/*
  Copyright 2025 Lukáš Růžička

  This file is part of exec_path_args.

  exec_path_args is free software: you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by the
  Free Software Foundation, either version 3 of the License, or (at your option)
  any later version.

  exec_path_args is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
  details.

  You should have received a copy of the GNU Lesser General Public License along
  with exec_path_args. If not, see <https://www.gnu.org/licenses/>.
*/

#include "impl/syscall_helper.hxx"

#include <cerrno>
#include <cstring>

#include <stdexcept>
#include <string>

namespace exec_path_args::os_wrapper {

int current_errno() noexcept { return errno; }

void check_syscall_ret_val(std::string_view const file, int const line,
                           int const syscall_ret) {
  // don't check the `errno` in advance (as was done in the past here) ... in
  // case some syscalls (graciously) failed before this one
  if (syscall_ret < 0) {
    auto const errno_val{current_errno()};
    throw std::runtime_error(std::string{file} + ":" + std::to_string(line) +
                             ": syscall failed - return code " +
                             std::to_string(syscall_ret) + ", errno " +
                             std::to_string(errno_val) + " ~ \"" +
                             std::strerror(errno_val) + "\"");
  }
};

} // namespace exec_path_args::os_wrapper

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

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "exec_path_args/pipe_helper.hxx"
#include "exec_path_args/process_handle_t.hxx"

namespace exec_path_args::os_wrapper {

struct exec_path_args {
  enum class state : char { uninitialzied, ready, running, finished };

  struct states {
    state previous;
    state current;
  };

  friend void swap(exec_path_args &lhs, exec_path_args &rhs) noexcept;

  exec_path_args() = default;

  explicit exec_path_args(std::string &&aPath,
                          std::vector<std::string> &&aArgs) noexcept
      : path{std::move(aPath)}, args{std::move(aArgs)}, current_state{
                                                            state::ready} {}

  [[nodiscard]] bool manages_process() const {
    return handle != invalid_process_handle;
  }

  exec_path_args(exec_path_args &&rhs) noexcept;
  exec_path_args &operator=(exec_path_args &&rhs) noexcept;

  ~exec_path_args() noexcept;

  // timeout_until_it_finishes_ms (as in
  // https://man7.org/linux/man-pages/man2/poll.2.html):
  // - negative -> wait indefinitely
  // - zero -> don't block
  // - positive -> wait up to given time
  [[nodiscard]] states
  update_and_get_state(int const timeout_until_it_finishes_ms = 0);

  [[nodiscard]] state finish_and_get_prev_state() {
    return update_and_get_state(-1).previous;
  }

  void finish() { [[maybe_unused]] auto const r{finish_and_get_prev_state()}; }

  [[nodiscard]] bool is_finished() const {
    return current_state == state::finished;
  }

  void send_to_stdin(std::string_view const data);
  void close_stdin();

  // updates corresponding internal buffer and returns:
  // `whole == false` -> only "increment" since it was called previously
  // `whole == true` -> everything since the start of the child process or last
  // call to the `get_...` counterpart
  [[nodiscard]] std::string_view read_stdout(bool const whole = false);
  [[nodiscard]] std::string_view read_stderr(bool const whole = false);

  // updates corresponding internal buffer and transfers its ownership:
  [[nodiscard]] std::string get_stdout();
  [[nodiscard]] std::string get_stderr();

  void do_kill();

  // - for running process, returns `current_time - time_spawned`
  // - it doesn't update status (e.g. if it already ended, this won't check it &
  // update internal state accordingly)
  // - once the process finishes, returned value is fixed
  [[nodiscard]] double time_running_ms() const;

  // NOTE: if the process was terminated by a signal, returns that signal number
  [[nodiscard]] int get_return_code() const;

  // `pid` of the child process
  [[nodiscard]] process_handle_t get_process_handle() const { return handle; }

private:
  exec_path_args(exec_path_args const &rhs) noexcept = delete;
  exec_path_args &operator=(exec_path_args const &rhs) noexcept = delete;

  std::string path;
  std::vector<std::string> args;

  long long time_spawned_ns{0};
  long long time_finished_ns{0};

  process_handle_t handle{invalid_process_handle};
  pipe_helper stdin_pipe;
  pipe_helper stdout_pipe;
  pipe_helper stderr_pipe;

  [[noreturn]] void exec_in_child(char *args[]);

  state current_state{state::uninitialzied};

  int return_code{};

  std::string stdout_buffer;
  ssize_t stdout_consumed_bytes{0};
  std::string stderr_buffer;
  ssize_t stderr_consumed_bytes{0};

  void query_status(bool const wait_for_finishing);

  void update_buffer(bool const for_stdout);
};

} // namespace exec_path_args::os_wrapper

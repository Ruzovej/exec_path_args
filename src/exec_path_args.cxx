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

#include "exec_path_args/exec_path_args.hxx"

#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>

#include "impl/syscall_helper.hxx"

namespace exec_path_args::os_wrapper {

namespace {

using timepoint_t = struct timespec;

// copied from
// <https://github.com/Ruzovej/cxxet/blob/main/include/public/cxxet/timepoint.hxx>
// & simplified:
[[nodiscard]] long long now_ns() {
  // https://stackoverflow.com/a/42658433
  // https://www.man7.org/linux/man-pages/man3/clock_gettime.3.html
  timepoint_t t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return static_cast<long long>(t.tv_sec * 1'000'000'000 + t.tv_nsec);
}

} // namespace

void swap(exec_path_args &lhs, exec_path_args &rhs) noexcept {
  using std::swap;

  swap(lhs.path, rhs.path);
  swap(lhs.args, rhs.args);
  swap(lhs.time_spawned_ns, rhs.time_spawned_ns);
  swap(lhs.time_finished_ns, rhs.time_finished_ns);
  swap(lhs.handle, rhs.handle);
  swap(lhs.stdin_pipe, rhs.stdin_pipe);
  swap(lhs.stdout_pipe, rhs.stdout_pipe);
  swap(lhs.stderr_pipe, rhs.stderr_pipe);
  swap(lhs.current_state, rhs.current_state);
  swap(lhs.return_code, rhs.return_code);
  swap(lhs.stdout_buffer, rhs.stdout_buffer);
  swap(lhs.stdout_consumed_bytes, rhs.stdout_consumed_bytes);
  swap(lhs.stderr_buffer, rhs.stderr_buffer);
  swap(lhs.stderr_consumed_bytes, rhs.stderr_consumed_bytes);
}

exec_path_args::exec_path_args(exec_path_args &&rhs) noexcept
    : path{std::move(rhs.path)}, args{std::move(rhs.args)},
      time_spawned_ns{rhs.time_spawned_ns},
      time_finished_ns{rhs.time_finished_ns}, handle{std::exchange(
                                                  rhs.handle,
                                                  invalid_process_handle)},
      stdin_pipe{std::move(rhs.stdin_pipe)},
      stdout_pipe{std::move(rhs.stdout_pipe)}, stderr_pipe{std::move(
                                                   rhs.stderr_pipe)},
      current_state{std::exchange(rhs.current_state, state::uninitialzied)},
      return_code{rhs.return_code}, stdout_buffer{std::move(rhs.stdout_buffer)},
      stdout_consumed_bytes{rhs.stdout_consumed_bytes}, stderr_buffer{std::move(
                                                            rhs.stderr_buffer)},
      stderr_consumed_bytes{rhs.stderr_consumed_bytes} {}

exec_path_args &exec_path_args::operator=(exec_path_args &&rhs) noexcept {
  if (this != &rhs) {
    exec_path_args tmp{std::move(rhs)};
    swap(*this, tmp);
  }
  return *this;
}

exec_path_args::~exec_path_args() noexcept {
  if (manages_process()) {
    do_kill(); // if this throws ... just let the OS "abort us".
  }
}

namespace {

struct my_cstr_arr_deleter {
  void operator()(char *null_terminated_arr[]) const {
    if (null_terminated_arr != nullptr) {
      for (char **ptr = null_terminated_arr; *ptr != nullptr; ++ptr) {
        std::free(*ptr); // due to `strdup`
      }
      delete[] null_terminated_arr;
    }
  }
};

[[nodiscard]] std::unique_ptr<char *[], my_cstr_arr_deleter>
build_args_cstr(std::string const &path, std::vector<std::string> const &args) {
  auto const num_args{args.size()};

  std::unique_ptr<char *[], my_cstr_arr_deleter> args_cstr_arr {
    new char *[num_args + 2], {}
  };

  // https://man7.org/linux/man-pages/man3/exec.3.html -> "The first
  // argument, by convention, should point to the filename associated with
  // the file being executed"
  args_cstr_arr[0] = strdup(path.c_str());
  for (std::size_t i{0}; i < num_args; ++i) {
    args_cstr_arr[i + 1] = strdup(args[i].c_str());
  }
  args_cstr_arr[num_args + 1] = nullptr;

  return args_cstr_arr;
}
} // namespace

exec_path_args::states
exec_path_args::update_and_get_state(int const timeout_until_it_finishes_ms) {
  auto const previous_state{current_state};

  switch (current_state) {
  case state::ready: {
    stdin_pipe.init();
    stdout_pipe.init();
    stderr_pipe.init();

    // it's safer to do as little after the `fork` and before `exec` as
    // possible:
    auto const argv{build_args_cstr(path, args)};

    auto const pid{EXEC_PATH_ARGS_SYSCALL_HELPER(fork())};
    if (pid == 0) // Child process
    {
      exec_in_child(argv.get());
    } // else ... parent process

    stdin_pipe.close_out();
    stdout_pipe.close_in();
    stderr_pipe.close_in();

    time_spawned_ns = now_ns();
    static_assert(
        std::is_same_v<std::decay_t<decltype(pid)>, process_handle_t>);
    handle = pid;
    current_state = state::running;

    if (timeout_until_it_finishes_ms != 0) {
      // IMHO safer than "fallthrough":
      return {previous_state,
              update_and_get_state(timeout_until_it_finishes_ms).current};
    }

    break;
  }
  case state::running: {
    if (!manages_process()) {
      throw std::runtime_error{
          "cannot update state - process handle is invalid!"};
    }

    // https://man7.org/linux/man-pages/man2/pidfd_open.2.html
    auto const pid_fd{EXEC_PATH_ARGS_SYSCALL_HELPER(static_cast<int>(
        syscall(static_cast<long>(SYS_pidfd_open), handle, 0)))};

    pollfd p_fd{pid_fd, POLLIN};

    // https://man7.org/linux/man-pages/man2/poll.2.html
    // https://stackoverflow.com/a/65003348/10712915
    auto const poll_res{EXEC_PATH_ARGS_SYSCALL_HELPER(
        poll(&p_fd, 1, timeout_until_it_finishes_ms))};

    if (poll_res == 1) {
      query_status(false);
    }

    if ((timeout_until_it_finishes_ms < 0) &&
        (current_state != state::finished)) {
      throw std::runtime_error{
          "failed to wait for child process to finish without any timeout!"};
    }

    break;
  }
  case state::finished: {
    if (!manages_process()) {
      throw std::runtime_error{
          "cannot update state - process handle is invalid!"};
    }
    // whatever ... nothing seems necessary here
    break;
  }
  case state::uninitialzied: {
    throw std::runtime_error{
        "cannot update state - process wasn't initialized!"};
  }
  default: {
    throw std::runtime_error{"unknown state!"};
  }
  }

  return {previous_state, current_state};
}

void exec_path_args::send_to_stdin(std::string_view const data) {
  if (!manages_process()) {
    throw std::runtime_error{
        "cannot write to inferior stdin - process handle is invalid!"};
  } else if (stdin_pipe.get_in() == invalid_fd) {
    throw std::runtime_error{"cannot write to inferior stdin - stdin pipe is "
                             "closed or not initialized!"};
  } else if (current_state != state::running) {
    throw std::runtime_error{
        "cannot write to inferior stdin - process isn't running!"};
  }

  ssize_t written{0};
  auto const data_size{static_cast<ssize_t>(
      data.size())}; // over-simplified version ... since
                     // `std::ssize` for `std::string_view` is C++20

  while (written < data_size) {
    auto const now_written{EXEC_PATH_ARGS_SYSCALL_HELPER(write(
        stdin_pipe.get_in(), data.data() + written, data_size - written))};
    written += now_written;
  }
}

void exec_path_args::close_stdin() {
  if (!manages_process()) {
    throw std::runtime_error{
        "cannot close inferior stdin - process handle is invalid!"};
  } else if ((current_state != state::running) ||
             (stdin_pipe.get_in() == invalid_fd)) {
    throw std::runtime_error{
        "cannot close inferior stdin - process isn't running or invalid fd!"};
  }
  stdin_pipe.close_in();
}

namespace {
std::string_view get_buffer(std::string const &buffer, ssize_t &consumed_bytes,
                            bool const whole) noexcept {
  if (whole) {
    consumed_bytes = buffer.size();
    return buffer;
  } else {
    std::string_view const ret{buffer.data() + consumed_bytes,
                               buffer.size() - consumed_bytes};
    consumed_bytes = buffer.size();
    return ret;
  }
}
} // namespace

std::string_view exec_path_args::read_stdout(bool const whole) {
  update_buffer(true);
  return get_buffer(stdout_buffer, stdout_consumed_bytes, whole);
}

std::string_view exec_path_args::read_stderr(bool const whole) {
  update_buffer(false);
  return get_buffer(stderr_buffer, stderr_consumed_bytes, whole);
}

std::string exec_path_args::get_stdout() {
  update_buffer(true);
  return std::move(stdout_buffer);
}

std::string exec_path_args::get_stderr() {
  update_buffer(false);
  return std::move(stderr_buffer);
}

void exec_path_args::do_kill() {
  if (manages_process() && (current_state == state::running)) {
    EXEC_PATH_ARGS_SYSCALL_HELPER(kill(handle, SIGKILL));
    query_status(true);
  }
}

double exec_path_args::time_running_ms() const {
  static auto constexpr time_diff_ms = [](long long const high_ns,
                                          long long const low_ns) -> double {
    return static_cast<double>(high_ns - low_ns) / 1'000'000;
  };

  if (!manages_process()) {
    throw std::runtime_error{"can't measure time - process handle is invalid!"};
  } else if (current_state == state::running) {
    return time_diff_ms(now_ns(), time_spawned_ns);
  } else if (current_state == state::finished) {
    return time_diff_ms(time_finished_ns, time_spawned_ns);
  } else {
    throw std::runtime_error{
        "cannot get running time - process isn't running or finished!"};
  }
}

int exec_path_args::get_return_code() const {
  if (!manages_process()) {
    throw std::runtime_error{
        "can't obtain return code - process handle is invalid!"};
  } else if (current_state != state::finished) {
    throw std::runtime_error{
        "can't obtain return code - process isn't finished!"};
  }
  return return_code;
}

void exec_path_args::exec_in_child(char *args[]) {
  try {
    stdin_pipe.close_in();
    EXEC_PATH_ARGS_SYSCALL_HELPER(dup2(stdin_pipe.get_out(), STDIN_FILENO));

    stdout_pipe.close_out();
    EXEC_PATH_ARGS_SYSCALL_HELPER(dup2(stdout_pipe.get_in(), STDOUT_FILENO));

    stderr_pipe.close_out();
    EXEC_PATH_ARGS_SYSCALL_HELPER(dup2(stderr_pipe.get_in(), STDERR_FILENO));

    // Execute the command
    EXEC_PATH_ARGS_SYSCALL_HELPER(execv(args[0], args));
  } catch (std::exception const &e) {
    // TODO is it safe to use `std::cerr` here (e.g. after `fork`, etc.)?
    std::cerr << "child process failed - caught exception: " << e.what()
              << '\n';
  }

  // Comment under <https://youtu.be/ki9omnMeYS8?si=WIVsmwHjcDxvwvlI> states:
  /*
  @keithmiller4358
  It's also worth mentioning that if you exit from signal handlers or after
  fork(), but before exec*(), you should use _exit() from posix, or std::_Exit()
  from c++11.  Anything called in such contexts must be async signal safe, and
  shouldn't malloc or free memory, or do anything else that isn't async signal
  safe. At least glibc has mutexes that can be left in inconsistent states in
  such contexts, causing hangs. This precludes using buffered i/o in signal
  handlers. This isn't an imaginary problem. I have fixed real cases of this in
  breakpad integration, threaded code that drops privelege by forking etc.
  Unless you like fixing weird hangs on internal glibc functions that occur
  randomly and rarely, burn this into your memory.
  */
  // made me rethink & rework it so ...
  // <https://en.cppreference.com/w/cpp/utility/program/_Exit> vs.
  // <https://en.cppreference.com/w/cpp/utility/program/exit.html>
  std::_Exit(EXIT_FAILURE);
}

void exec_path_args::query_status(bool const wait_for_finishing) {
  if (!manages_process()) {
    throw std::runtime_error{"can't query status - process handle is invalid!"};
  } else if (current_state == state::running) {
    siginfo_t status{};
    int const options{WEXITED | (wait_for_finishing ? 0 : WNOHANG)};
    // https://man7.org/linux/man-pages/man2/wait.2.html
    EXEC_PATH_ARGS_SYSCALL_HELPER(waitid(P_PID, handle, &status, options));

    if ((status.si_pid != 0) &&
        ((status.si_code == CLD_EXITED) || (status.si_code == CLD_KILLED) ||
         (status.si_code == CLD_DUMPED))) {
      if (status.si_pid != handle) {
        throw std::runtime_error{
            "waitid returned unexpected pid - different from the managed one!"};
      }
      time_finished_ns = now_ns();
      current_state = state::finished;
      return_code =
          status.si_status; // or signal ... don't make a difference here
    }
  } else if (current_state == state::finished) {
    return;
  } else {
    throw std::runtime_error{
        "cannot wait for pid - process isn't running or finished!"};
  }
}

void exec_path_args::update_buffer(bool const for_stdout) {
  if (!manages_process()) {
    throw std::runtime_error{
        "cannot update any buffer - process handle is invalid!"};
  }

  static auto constexpr read_pipe = [](native_fd_t const fd,
                                       std::string &buffer) {
    if (fd == invalid_fd) {
      throw std::runtime_error{
          "cannot read from given pipe - it's closed or not initialized!"};
    }

    auto const buf_prev_size{static_cast<ssize_t>(buffer.size())};

    int avail{0};
    EXEC_PATH_ARGS_SYSCALL_HELPER(ioctl(fd, FIONREAD, &avail));

    ssize_t nbytes{0};
    if (0 < avail) // TODO read in a loop (in case `nbytes` < `avail`)?!
    {
      buffer.resize(static_cast<size_t>(buf_prev_size + avail));

      //#if defined(__clang__)
      //      // TODO get rid of the ugly `const_cast`
      //      nbytes = EXEC_PATH_ARGS_SYSCALL_HELPER(
      //          read(fd, const_cast<char *>(buffer.data()) + buf_prev_size,
      //          avail));
      //#else
      nbytes = EXEC_PATH_ARGS_SYSCALL_HELPER(
          read(fd, buffer.data() + buf_prev_size, avail));
      //#endif
    }
    if (nbytes < avail) {
      throw std::runtime_error(
          "failed to read all available bytes from given pipe!");
    }
  };

  read_pipe(for_stdout ? stdout_pipe.get_out() : stderr_pipe.get_out(),
            for_stdout ? stdout_buffer : stderr_buffer);
}

} // namespace exec_path_args::os_wrapper

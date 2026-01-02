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

#include <csignal>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <new>
#include <optional>

#include <doctest/doctest.h>

#include <ips/ips.hxx>

namespace exec_path_args::os_wrapper {
namespace {

std::string space_to_newline(std::string str) {
  std::transform(str.begin(), str.end(), str.begin(),
                 [](char c) { return c == ' ' ? '\n' : c; });
  return str;
}

TEST_CASE("exec_path_args") {
  SUBCASE("simple shell command") {
    static auto constexpr shell_cmd = [](std::string &&cmd_str) {
      exec_path_args cmd{"/usr/bin/env", {"sh", "-c", std::move(cmd_str)}};

      REQUIRE_FALSE(cmd.manages_process());
      REQUIRE_FALSE(cmd.is_finished());
      return cmd;
    };

    SUBCASE("happy path - nonblocking") {
      exec_path_args cmd{
          shell_cmd("printf \"Hello stdout!\"; printf \"Hello stderr!\" 1>&2")};

      {
        exec_path_args::states state;
        REQUIRE_NOTHROW(state = cmd.update_and_get_state());
        REQUIRE_EQ(state.previous, exec_path_args::state::ready);
        REQUIRE_EQ(state.current, exec_path_args::state::running);
        REQUIRE(cmd.manages_process());
      }

      {
        exec_path_args::states state;
        REQUIRE_NOTHROW(state = cmd.update_and_get_state());
        REQUIRE_EQ(state.previous, exec_path_args::state::running);
        WARN_EQ(state.current, exec_path_args::state::running);
      }
      // potential for flakiness -> hence there are `WARN`s on this "boundary"
      {
        exec_path_args::state prev_state;
        REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
        WARN_EQ(prev_state, exec_path_args::state::running);
      }

      // querying again won't change anything
      for (int i{0}; i < 2; ++i) {
        {
          exec_path_args::states state;
          REQUIRE_NOTHROW(state = cmd.update_and_get_state());
          REQUIRE_EQ(state.previous, exec_path_args::state::finished);
          REQUIRE_EQ(state.current, exec_path_args::state::finished);
        }

        REQUIRE_EQ(cmd.read_stdout(true), "Hello stdout!");
        REQUIRE_EQ(cmd.read_stdout(false), "");

        REQUIRE_EQ(cmd.read_stderr(true), "Hello stderr!");
        REQUIRE_EQ(cmd.read_stderr(false), "");

        REQUIRE_EQ(cmd.get_return_code(), EXIT_SUCCESS);
        REQUIRE_LT(0.0, cmd.time_running_ms());
      }

      // consume outputs ...
      REQUIRE_EQ(cmd.get_stdout(), "Hello stdout!");
      REQUIRE_EQ(cmd.get_stdout(), "");
      REQUIRE_EQ(cmd.read_stdout(true), "");

      REQUIRE_EQ(cmd.get_stderr(), "Hello stderr!");
      REQUIRE_EQ(cmd.get_stderr(), "");
      REQUIRE_EQ(cmd.read_stderr(true), "");
    }

    SUBCASE("happy path - blocking") {
      exec_path_args cmd{
          shell_cmd("printf \"Hello stdout!\"; printf \"Hello stderr!\" 1>&2")};

      {
        exec_path_args::state prev_state;
        REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
        REQUIRE_EQ(prev_state, exec_path_args::state::ready);
        REQUIRE(cmd.manages_process());
      }

      REQUIRE_EQ(cmd.read_stdout(true), "Hello stdout!");
      REQUIRE_EQ(cmd.read_stderr(true), "Hello stderr!");
      REQUIRE_EQ(cmd.get_return_code(), EXIT_SUCCESS);
      REQUIRE_LT(0.0, cmd.time_running_ms());
    }

    SUBCASE("non-zero return code") {
      static auto constexpr expected_val{42};
      exec_path_args cmd{shell_cmd("exit " + std::to_string(expected_val))};

      {
        exec_path_args::state prev_state;
        REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
        REQUIRE_EQ(prev_state, exec_path_args::state::ready);
        REQUIRE(cmd.manages_process());
      }

      REQUIRE_EQ(cmd.read_stdout(true), "");
      REQUIRE_EQ(cmd.read_stderr(true), "");
      REQUIRE_EQ(cmd.get_return_code(), expected_val);
      REQUIRE_LT(0.0, cmd.time_running_ms());
    }

    SUBCASE("not waiting for it") {
      // in `shell`, `sleep` accepts seconds
      exec_path_args cmd{shell_cmd("sleep 1; printf \"Done!\"")};

      {
        exec_path_args::states state;
        REQUIRE_NOTHROW(state = cmd.update_and_get_state());
        REQUIRE_EQ(state.previous, exec_path_args::state::ready);
        REQUIRE_EQ(state.current, exec_path_args::state::running);
        REQUIRE(cmd.manages_process());
      }

      REQUIRE_NOTHROW(cmd.do_kill()); // it should get there safely, way sooner
                                      // than 1 second after forking ...
      REQUIRE(cmd.is_finished());

      SUBCASE("explicitly updating the status after kill") {
        exec_path_args::state prev_state;
        REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
        REQUIRE_EQ(prev_state, exec_path_args::state::finished);
      }

      SUBCASE("not updating the status after kill") {
        // is expetected to be equivalent ...
      }

      REQUIRE(cmd.is_finished());
      REQUIRE_EQ(cmd.read_stdout(true), "");
      REQUIRE_EQ(cmd.read_stderr(true), "");
      REQUIRE_NE(cmd.get_return_code(), EXIT_SUCCESS);
      REQUIRE_LT(0.0, cmd.time_running_ms());
    }

    SUBCASE("various operations") {
      SUBCASE("move opearations") {
        std::optional<exec_path_args> cmd{shell_cmd("echo Hello")};
        REQUIRE_FALSE(cmd->manages_process());

        SUBCASE("not spawned yet") {
          SUBCASE("ctor") {
            exec_path_args cmd2{std::move(*cmd)};
            REQUIRE_FALSE(cmd2.manages_process());
            REQUIRE_FALSE(cmd->manages_process());
          }

          SUBCASE("assignment") {
            exec_path_args cmd2;
            auto const ptr{std::launder(&cmd2)}; // prevent optimizations ...
            *ptr = std::move(*cmd);
            REQUIRE_FALSE(ptr->manages_process());
            REQUIRE_FALSE(cmd->manages_process());
          }
        }

        SUBCASE("just spawned") {
          {
            exec_path_args::states state;
            REQUIRE_NOTHROW(state = cmd->update_and_get_state());
            REQUIRE_EQ(state.previous, exec_path_args::state::ready);
            REQUIRE_EQ(state.current, exec_path_args::state::running);
            REQUIRE(cmd->manages_process());
          }

          exec_path_args cmd2{std::move(*cmd)};

          SUBCASE("without imediate reset") {}

          SUBCASE("with imediate reset") { cmd.reset(); }

          REQUIRE(cmd2.manages_process());
          REQUIRE_FALSE(cmd->manages_process());

          {
            exec_path_args::state prev_state;
            REQUIRE_THROWS(prev_state = cmd->finish_and_get_prev_state());
            REQUIRE_NOTHROW(prev_state = cmd2.finish_and_get_prev_state());
            REQUIRE_EQ(prev_state, exec_path_args::state::running);
          }

          REQUIRE_EQ(cmd2.read_stdout(true), "Hello\n");
          REQUIRE_EQ(cmd2.read_stderr(true), "");
          REQUIRE_EQ(cmd2.get_return_code(), EXIT_SUCCESS);
          REQUIRE_LT(0.0, cmd2.time_running_ms());
        }

        SUBCASE("after finishing") {
          {
            exec_path_args::state prev_state;
            REQUIRE_NOTHROW(prev_state = cmd->finish_and_get_prev_state());
            REQUIRE_EQ(prev_state, exec_path_args::state::ready);
          }

          exec_path_args cmd2{std::move(*cmd)};

          SUBCASE("without imediate reset") {}

          SUBCASE("with imediate reset") { cmd.reset(); }

          REQUIRE(cmd2.manages_process());
          REQUIRE_FALSE(cmd->manages_process());

          REQUIRE_EQ(cmd2.read_stdout(true), "Hello\n");
          REQUIRE_EQ(cmd2.read_stderr(true), "");
          REQUIRE_EQ(cmd2.get_return_code(), EXIT_SUCCESS);
          REQUIRE_LT(0.0, cmd2.time_running_ms());
        }

        cmd.reset();
      }

      SUBCASE("operations on un-started process") {
        exec_path_args cmd_default_constructed{};

        exec_path_args cmd_moved_from{
            shell_cmd("whatever ... won't be started for the purpose of the "
                      "test case ...")};

        exec_path_args cmd_move_constructed{std::move(cmd_default_constructed)};

        SUBCASE("state checks") {
          REQUIRE_FALSE(cmd_default_constructed.manages_process());
          REQUIRE_FALSE(cmd_default_constructed.is_finished());

          REQUIRE_FALSE(cmd_moved_from.manages_process());
          REQUIRE_FALSE(cmd_moved_from.is_finished());

          REQUIRE_FALSE(cmd_move_constructed.manages_process());
          REQUIRE_FALSE(cmd_move_constructed.is_finished());
        }

        SUBCASE("stdin operations") {
          REQUIRE_THROWS(cmd_default_constructed.send_to_stdin("data"));
          REQUIRE_THROWS(cmd_default_constructed.close_stdin());

          REQUIRE_THROWS(cmd_moved_from.send_to_stdin("data"));
          REQUIRE_THROWS(cmd_moved_from.close_stdin());

          REQUIRE_THROWS(cmd_move_constructed.send_to_stdin("data"));
          REQUIRE_THROWS(cmd_move_constructed.close_stdin());
        }

        SUBCASE("stdout & stderr operations") {
          [[maybe_unused]] std::string_view str;

          REQUIRE_THROWS(str = cmd_default_constructed.read_stdout());
          REQUIRE_THROWS(str = cmd_default_constructed.read_stderr());

          REQUIRE_THROWS(str = cmd_moved_from.read_stdout());
          REQUIRE_THROWS(str = cmd_moved_from.read_stderr());

          REQUIRE_THROWS(str = cmd_move_constructed.read_stdout());
          REQUIRE_THROWS(str = cmd_move_constructed.read_stderr());
        }

        SUBCASE("termination related") {
          [[maybe_unused]] int ret_code;
          [[maybe_unused]] double time_ms;

          REQUIRE_THROWS(ret_code = cmd_default_constructed.get_return_code());
          REQUIRE_THROWS(time_ms = cmd_default_constructed.time_running_ms());

          REQUIRE_THROWS(ret_code = cmd_moved_from.get_return_code());
          REQUIRE_THROWS(time_ms = cmd_moved_from.time_running_ms());

          REQUIRE_THROWS(ret_code = cmd_move_constructed.get_return_code());
          REQUIRE_THROWS(time_ms = cmd_move_constructed.time_running_ms());

          // this one has checks inside, because it's used in d-tor ...:
          REQUIRE_NOTHROW(cmd_default_constructed.do_kill());

          REQUIRE_NOTHROW(cmd_moved_from.do_kill());

          REQUIRE_NOTHROW(cmd_move_constructed.do_kill());
        }
      }

      SUBCASE("swap") {
        exec_path_args cmd1{shell_cmd("echo cmd1; exit 1")};
        exec_path_args cmd2{shell_cmd("echo cmd2; exit 2")};

        {
          exec_path_args::state prev_state;
          REQUIRE_NOTHROW(prev_state = cmd1.finish_and_get_prev_state());
          REQUIRE_EQ(prev_state, exec_path_args::state::ready);
        }

        {
          exec_path_args::state prev_state;
          REQUIRE_NOTHROW(prev_state = cmd2.finish_and_get_prev_state());
          REQUIRE_EQ(prev_state, exec_path_args::state::ready);
        }

        swap(cmd1, cmd2);

        REQUIRE_EQ(cmd1.read_stdout(true), "cmd2\n");
        REQUIRE_EQ(cmd1.get_return_code(), 2);
        REQUIRE_LT(0.0, cmd1.time_running_ms());

        REQUIRE_EQ(cmd2.read_stdout(true), "cmd1\n");
        REQUIRE_EQ(cmd2.get_return_code(), 1);
        REQUIRE_LT(0.0, cmd2.time_running_ms());
      }
    }
  }

  SUBCASE("some_cli_app") {
    auto const some_cli_app_path{std::filesystem::current_path() /
                                 "build/tests/unit/some_cli_app"};

    REQUIRE(std::filesystem::exists(some_cli_app_path));
    REQUIRE(std::filesystem::is_regular_file(some_cli_app_path));

    auto const some_cli_app = [&](auto &&...args) {
      exec_path_args cmd{some_cli_app_path.string(),
                         {std::forward<decltype(args)>(args)...}};

      REQUIRE_FALSE(cmd.manages_process());
      REQUIRE_FALSE(cmd.is_finished());
      return cmd;
    };

    SUBCASE("basic functionality, without synchronization") {
      SUBCASE("exit") {
        auto const exit_code{"11"};
        exec_path_args cmd{some_cli_app("--exit", exit_code)};

        {
          exec_path_args::state prev_state;
          REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
          REQUIRE_EQ(prev_state, exec_path_args::state::ready);
          REQUIRE(cmd.manages_process());
        }

        REQUIRE_EQ(cmd.read_stderr(true), "");
        REQUIRE_EQ(cmd.read_stdout(true), "");
        REQUIRE_EQ(cmd.get_return_code(), std::stoi(exit_code));
        REQUIRE_LT(0.0, cmd.time_running_ms());
      }

      SUBCASE("exit is always last action") {
        auto const exit_code{"12"};
        exec_path_args cmd{some_cli_app("--exit", exit_code,            // ...
                                        "--stdout", "won't be printed", // ...
                                        "--notify-and-wait"             // ...
                                        )};

        {
          exec_path_args::state prev_state;
          REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
          REQUIRE_EQ(prev_state, exec_path_args::state::ready);
          REQUIRE(cmd.manages_process());
        }

        REQUIRE_EQ(cmd.read_stderr(true), "");
        REQUIRE_EQ(cmd.read_stdout(true), "");
        REQUIRE_EQ(cmd.get_return_code(), std::stoi(exit_code));
        REQUIRE_LT(0.0, cmd.time_running_ms());
      }

      SUBCASE("sleep") {
        exec_path_args cmd{some_cli_app("--sleep", "1")};

        {
          exec_path_args::state prev_state;
          REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
          REQUIRE_EQ(prev_state, exec_path_args::state::ready);
          REQUIRE(cmd.manages_process());
        }

        REQUIRE_EQ(cmd.read_stderr(true), "");
        REQUIRE_EQ(cmd.read_stdout(true), "");
        REQUIRE_EQ(cmd.get_return_code(), EXIT_SUCCESS);
        REQUIRE_LT(0.0, cmd.time_running_ms());
      }

      SUBCASE("stdout") {
        auto const text{"Hello!"};
        exec_path_args cmd{some_cli_app("--stdout", text)};

        {
          exec_path_args::state prev_state;
          REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
          REQUIRE_EQ(prev_state, exec_path_args::state::ready);
          REQUIRE(cmd.manages_process());
        }

        std::string const expected{std::string{text} + '\n'};

        REQUIRE_EQ(cmd.read_stderr(true), "");
        REQUIRE_EQ(cmd.read_stdout(false), expected); // not yet consumed ...
        REQUIRE_EQ(cmd.read_stdout(true), expected);
        REQUIRE_EQ(cmd.read_stdout(false), "");      // consumed ...
        REQUIRE_EQ(cmd.read_stdout(true), expected); // still reachable
        REQUIRE_EQ(cmd.read_stdout(false), "");      // still consumed ...
        REQUIRE_EQ(cmd.get_return_code(), EXIT_SUCCESS);
        REQUIRE_LT(0.0, cmd.time_running_ms());
      }

      SUBCASE("sleep interupted") {
        exec_path_args cmd{some_cli_app("--sleep", "1000",             // ...
                                        "--stdout", "won't be printed" // ...
                                        )};

        {
          exec_path_args::states state;
          REQUIRE_NOTHROW(state = cmd.update_and_get_state());
          REQUIRE_EQ(state.previous, exec_path_args::state::ready);
          REQUIRE_EQ(state.current, exec_path_args::state::running);
          REQUIRE(cmd.manages_process());
        }

        REQUIRE_NOTHROW(cmd.do_kill());
        REQUIRE(cmd.is_finished());

        {
          exec_path_args::state prev_state;
          REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
          REQUIRE_EQ(prev_state, exec_path_args::state::finished);
        }

        REQUIRE_EQ(cmd.read_stderr(true), "");
        REQUIRE_EQ(cmd.read_stdout(true), "");
        REQUIRE_EQ(cmd.get_return_code(), SIGKILL);
        REQUIRE_LT(0.0, cmd.time_running_ms());
      }

      SUBCASE("stderr") {
        auto const text{"Hello!"};
        exec_path_args cmd{some_cli_app("--stderr", text)};

        {
          exec_path_args::state prev_state;
          REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
          REQUIRE_EQ(prev_state, exec_path_args::state::ready);
          REQUIRE(cmd.manages_process());
        }

        std::string const expected{std::string{text} + '\n'};

        REQUIRE_EQ(cmd.read_stderr(false), expected);
        REQUIRE_EQ(cmd.read_stderr(true), expected);
        REQUIRE_EQ(cmd.read_stderr(false), "");      // ditto ...
        REQUIRE_EQ(cmd.read_stderr(true), expected); // ...
        REQUIRE_EQ(cmd.read_stderr(false), "");      // ...
        REQUIRE_EQ(cmd.read_stdout(true), "");
        REQUIRE_EQ(cmd.get_return_code(), EXIT_SUCCESS);
        REQUIRE_LT(0.0, cmd.time_running_ms());
      }

      SUBCASE("invalid argument") {
        auto const exit_code{"13"};
        exec_path_args cmd{some_cli_app("--exit", exit_code, "--invalid")};

        {
          exec_path_args::state prev_state;
          REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
          REQUIRE_EQ(prev_state, exec_path_args::state::ready);
          REQUIRE(cmd.manages_process());
        }

        REQUIRE_EQ(cmd.read_stderr(true),
                   "some_cli_app caught `input_exception`: Unknown argument: "
                   "--invalid\n");
        REQUIRE_EQ(cmd.read_stdout(true), "");
        REQUIRE_NE(cmd.get_return_code(), std::stoi(exit_code));
        REQUIRE_EQ(cmd.get_return_code(), EXIT_FAILURE);
        REQUIRE_LT(0.0, cmd.time_running_ms());
      }

      SUBCASE("stdout & stderr") {
        auto const text{"Hello!"};
        exec_path_args cmd{some_cli_app("--stdout", text, "--stderr", text)};

        {
          exec_path_args::state prev_state;
          REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
          REQUIRE_EQ(prev_state, exec_path_args::state::ready);
          REQUIRE(cmd.manages_process());
        }

        std::string const expected{std::string{text} + '\n'};
        REQUIRE_EQ(cmd.read_stdout(true), expected);
        REQUIRE_EQ(cmd.read_stdout(false), "");
        REQUIRE_EQ(cmd.read_stdout(true), expected);
        REQUIRE_EQ(cmd.read_stdout(false), "");
        REQUIRE_EQ(cmd.read_stderr(true), expected);
        REQUIRE_EQ(cmd.read_stderr(false), "");
        REQUIRE_EQ(cmd.read_stderr(true), expected);
        REQUIRE_EQ(cmd.read_stderr(false), "");
        REQUIRE_EQ(cmd.get_return_code(), EXIT_SUCCESS);
        REQUIRE_LT(0.0, cmd.time_running_ms());
      }

      SUBCASE("echo 1") {
        exec_path_args cmd{some_cli_app("--echo", "1")};

        {
          exec_path_args::states state;
          REQUIRE_NOTHROW(state = cmd.update_and_get_state());
          REQUIRE_EQ(state.previous, exec_path_args::state::ready);
          REQUIRE_EQ(state.current, exec_path_args::state::running);
          REQUIRE(cmd.manages_process());
        }

        auto const text{"Hello! "}; // see the ' ' at the end
        REQUIRE_NOTHROW(cmd.send_to_stdin(text));

        // close it
        REQUIRE_NOTHROW(cmd.close_stdin());
        // exceptions will indicate that nothing more can be passed to it
        REQUIRE_THROWS(cmd.close_stdin());
        REQUIRE_THROWS(cmd.send_to_stdin(text));

        {
          exec_path_args::state prev_state;
          REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
          REQUIRE_EQ(prev_state, exec_path_args::state::running);
        }

        REQUIRE_EQ(cmd.read_stderr(true), "");
        REQUIRE_EQ(cmd.read_stdout(true), space_to_newline(text));
        REQUIRE_EQ(cmd.get_return_code(), EXIT_SUCCESS);
        REQUIRE_LT(0.0, cmd.time_running_ms());
      }

      SUBCASE("echo 2") {
        exec_path_args cmd{some_cli_app("--echo", "1")};

        {
          exec_path_args::states state;
          REQUIRE_NOTHROW(state = cmd.update_and_get_state());
          REQUIRE_EQ(state.previous, exec_path_args::state::ready);
          REQUIRE_EQ(state.current, exec_path_args::state::running);
          REQUIRE(cmd.manages_process());
        }

        auto const text{"Hello!"}; // NO ' ' at the end
        REQUIRE_NOTHROW(cmd.send_to_stdin(text));
        REQUIRE_NOTHROW(cmd.close_stdin());

        {
          exec_path_args::state prev_state;
          REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
          REQUIRE_EQ(prev_state, exec_path_args::state::running);
        }

        REQUIRE_EQ(cmd.read_stderr(true), "");
        REQUIRE_EQ(cmd.read_stdout(true), space_to_newline(text) + '\n');
        REQUIRE_EQ(cmd.get_return_code(), EXIT_SUCCESS);
        REQUIRE_LT(0.0, cmd.time_running_ms());
      }

      SUBCASE("handled exception") {
        auto const exit_code{"14"};
        auto const exception_text{"handled"};
        exec_path_args cmd{some_cli_app("--handled-exception",
                                        exception_text, // ...
                                                        // won't be reached:
                                        "--exit", exit_code,           // ...
                                        "--stdout", "won't be printed" // ...
                                        )};

        {
          exec_path_args::state prev_state;
          REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
          REQUIRE_EQ(prev_state, exec_path_args::state::ready);
          REQUIRE(cmd.manages_process());
        }

        REQUIRE_EQ(cmd.read_stderr(true),
                   "some_cli_app caught `std::exception`: handled\n");
        REQUIRE_EQ(cmd.read_stdout(true), "");
        REQUIRE_NE(cmd.get_return_code(), std::stoi(exit_code));
        REQUIRE_EQ(cmd.get_return_code(), EXIT_FAILURE);
        REQUIRE_LT(0.0, cmd.time_running_ms());
      }

      SUBCASE("unhandled exception") {
        auto const exit_code{"15"};
        auto const exception_text{"unhandled"};
        exec_path_args cmd{some_cli_app("--unhandled-exception",
                                        exception_text, // ...
                                                        // won't be reached:
                                        "--handled-exception",
                                        exception_text,                // ...
                                        "--exit", exit_code,           // ...
                                        "--stdout", "won't be printed" // ...
                                        )};

        {
          exec_path_args::state prev_state;
          REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
          REQUIRE_EQ(prev_state, exec_path_args::state::ready);
          REQUIRE(cmd.manages_process());
        }

        WARN_EQ(cmd.read_stderr(true),
                "terminate called after throwing an instance of 'char "
                "const*'\n"); // IMHO OS dependent ... -> only `WARN`
        REQUIRE_EQ(cmd.read_stdout(true), "");
        REQUIRE_NE(cmd.get_return_code(), std::stoi(exit_code));
        REQUIRE_EQ(cmd.get_return_code(), SIGABRT);
        REQUIRE_LT(0.0, cmd.time_running_ms());
      }
    }

    SUBCASE("synchronized") {
      auto const sem_name{"/some_cli_app_shared_sem"};

      auto const some_cli_app_synced = [&](auto &&...args) {
        return some_cli_app("--sem-name", sem_name,
                            std::forward<decltype(args)>(args)...);
      };

      // compromise between stalls (in case of a failure) and flakiness; feel
      // free to (slightly?!) increase this number if necessary
      static int constexpr default_wait_timeout_ms{5};

      std::optional<ips> my_sem;
      // using `std::optional` for a single purpose - so the c-tor can be
      // checked for not throwing ...:
      REQUIRE_NOTHROW(my_sem.emplace(sem_name, true));

      SUBCASE("basic functionality: interprocess synchronization") {
        auto const exit_code{"16"};
        exec_path_args cmd{
            some_cli_app_synced("--notify-and-wait", "--exit", exit_code)};

        {
          exec_path_args::states state;
          REQUIRE_NOTHROW(state = cmd.update_and_get_state());
          REQUIRE_EQ(state.previous, exec_path_args::state::ready);
          REQUIRE_EQ(state.current, exec_path_args::state::running);
          REQUIRE(cmd.manages_process());
        }

        REQUIRE(my_sem->wait_and_notify(default_wait_timeout_ms));

        {
          exec_path_args::state prev_state;
          REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
          REQUIRE_EQ(prev_state, exec_path_args::state::running);
        }

        REQUIRE_EQ(cmd.read_stderr(true), "");
        REQUIRE_EQ(cmd.read_stdout(true), "");
        REQUIRE_EQ(cmd.get_return_code(), std::stoi(exit_code));
        REQUIRE_LT(0.0, cmd.time_running_ms());
      }

      SUBCASE("uninitialized `ips` in parent process") {
        my_sem.reset(); // destroy it here, in the parent process

        exec_path_args cmd{some_cli_app_synced("--notify-and-wait", // ...
                                               "--exit", "0"        // ...
                                               )};

        {
          exec_path_args::state prev_state;
          REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
          REQUIRE_EQ(prev_state, exec_path_args::state::ready);
        }

        REQUIRE_EQ(
            cmd.read_stderr(true),
            "some_cli_app caught `std::exception`: sem_open failed: 2\n");
        REQUIRE_EQ(cmd.read_stdout(true), "");
        REQUIRE_EQ(cmd.get_return_code(), EXIT_FAILURE);
        REQUIRE_LT(0.0, cmd.time_running_ms());
      }

      SUBCASE("uninitialized `ips` in child process") {
        // not calling the `some_cli_app_synced` helper here ...:
        exec_path_args cmd{some_cli_app("--notify-and-wait", // ...
                                        "--exit", "0"        // ...
                                        )};

        {
          exec_path_args::states state;
          REQUIRE_NOTHROW(state = cmd.update_and_get_state());
          REQUIRE_EQ(state.previous, exec_path_args::state::ready);
          REQUIRE_EQ(state.current, exec_path_args::state::running);
          REQUIRE(cmd.manages_process());
        }

        REQUIRE_FALSE(my_sem->wait_and_notify(
            1)); // so it doesn't waste too much time ...

        {
          exec_path_args::state prev_state;
          REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
          REQUIRE_EQ(prev_state, exec_path_args::state::running);
        }

        REQUIRE_EQ(cmd.read_stderr(true),
                   "some_cli_app caught `std::exception`: Semaphore name not "
                   "specified for sync operation\n");
        REQUIRE_EQ(cmd.read_stdout(true), "");
        REQUIRE_EQ(cmd.get_return_code(), EXIT_FAILURE);
        REQUIRE_LT(0.0, cmd.time_running_ms());
      }

      SUBCASE("missed `ips` in child process") {
        exec_path_args cmd{some_cli_app_synced("--sleep", "1000",   // ...
                                               "--notify-and-wait", // ...
                                               "--exit", "0"        // ...
                                               )};

        {
          exec_path_args::states state;
          REQUIRE_NOTHROW(state = cmd.update_and_get_state());
          REQUIRE_EQ(state.previous, exec_path_args::state::ready);
          REQUIRE_EQ(state.current, exec_path_args::state::running);
          REQUIRE(cmd.manages_process());
        }

        REQUIRE_FALSE(my_sem->wait_and_notify(
            1)); // so it doesn't waste too much time ...

        {
          exec_path_args::states state;
          REQUIRE_NOTHROW(state = cmd.update_and_get_state());
          REQUIRE_EQ(state.previous, exec_path_args::state::running);
          REQUIRE_EQ(state.current, exec_path_args::state::running);
        }

        REQUIRE_NOTHROW(cmd.do_kill());
        REQUIRE(cmd.is_finished());

        {
          exec_path_args::state prev_state;
          REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
          // e.g. redundant, but valid ...:
          REQUIRE_EQ(prev_state, exec_path_args::state::finished);
        }

        REQUIRE_EQ(cmd.read_stderr(true), "");
        REQUIRE_EQ(cmd.read_stdout(true), "");
        REQUIRE_EQ(cmd.get_return_code(), SIGKILL);
        REQUIRE_LT(0.0, cmd.time_running_ms());
      }

      SUBCASE("echo 3 (synced)") {
        exec_path_args cmd{some_cli_app_synced("--echo", "1",      // ...
                                               "--notify-and-wait" // ...
                                               )};

        {
          exec_path_args::states state;
          REQUIRE_NOTHROW(state = cmd.update_and_get_state());
          REQUIRE_EQ(state.previous, exec_path_args::state::ready);
          REQUIRE_EQ(state.current, exec_path_args::state::running);
          REQUIRE(cmd.manages_process());
        }

        auto const text{"Hello!"}; // NO ' ' at the end
        REQUIRE_NOTHROW(cmd.send_to_stdin(text));
        REQUIRE_NOTHROW(cmd.close_stdin());

        REQUIRE(my_sem->wait_and_notify(default_wait_timeout_ms));

        REQUIRE_EQ(cmd.read_stderr(true), "");
        REQUIRE_EQ(cmd.read_stdout(true), space_to_newline(text) + '\n');

        {
          exec_path_args::state prev_state;
          REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
          REQUIRE_EQ(prev_state, exec_path_args::state::running);
        }

        REQUIRE_EQ(cmd.read_stderr(false), "");
        REQUIRE_EQ(cmd.read_stdout(false), "");
        REQUIRE_EQ(cmd.get_return_code(), EXIT_SUCCESS);
        REQUIRE_LT(0.0, cmd.time_running_ms());
      }

      SUBCASE("complex happy path") {
        auto const to_stderr{"How is it going?"};
        auto const to_stdout{"Fine, thank You!"};
        auto const exit_code{"17"};
        exec_path_args cmd{some_cli_app_synced("--stderr", to_stderr, // 1
                                               "--stdout", to_stdout, // 2
                                               "--sleep", "1",        // 3
                                               "--notify-and-wait",   // ...
                                               "--echo", "3",         // 4
                                               "--notify-and-wait",   // ...
                                               "--exit", exit_code    // 5
                                               )};

        {
          exec_path_args::states state;
          REQUIRE_NOTHROW(state = cmd.update_and_get_state());
          REQUIRE_EQ(state.previous, exec_path_args::state::ready);
          REQUIRE_EQ(state.current, exec_path_args::state::running);
          REQUIRE(cmd.manages_process());
        }

        REQUIRE(my_sem->wait_and_notify(default_wait_timeout_ms));

        REQUIRE_EQ(cmd.read_stdout(true), std::string{to_stdout} + '\n');
        REQUIRE_EQ(cmd.read_stderr(true), std::string{to_stderr} + '\n');

        {
          exec_path_args::states state;
          REQUIRE_NOTHROW(state = cmd.update_and_get_state());
          REQUIRE_EQ(state.previous, exec_path_args::state::running);
          REQUIRE_EQ(state.current, exec_path_args::state::running);
        }

        // pay attention to the space at the end ...:
        auto const expected_echo_input{"const std::string_view data "};
        REQUIRE_NOTHROW(cmd.send_to_stdin(expected_echo_input));

        REQUIRE(my_sem->wait_and_notify(default_wait_timeout_ms));

        REQUIRE_EQ(cmd.read_stdout(false),
                   space_to_newline(expected_echo_input));
        REQUIRE_EQ(cmd.read_stdout(false), ""); // consumed in previous line
        REQUIRE_EQ(cmd.read_stderr(false), "");

        {
          exec_path_args::state prev_state;
          REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
          REQUIRE_EQ(prev_state, exec_path_args::state::running);
        }

        REQUIRE_EQ(cmd.get_return_code(), std::stoi(exit_code));
        REQUIRE_LT(0.0, cmd.time_running_ms());
      }

      SUBCASE("continuous output consumption") {
        auto const out1{"out 111"};
        auto const out2{"out 222"};
        auto const err1{"err 111"};
        auto const err2{"err 222"};
        auto const exit_code{"18"};
        exec_path_args cmd{some_cli_app_synced("--stdout", out1,    // ...
                                               "--notify-and-wait", // ...
                                               "--stderr", err1,    // ...
                                               "--notify-and-wait", // ...
                                               "--stdout", out2,    // ...
                                               "--notify-and-wait", // ...
                                               "--stderr", err2,    // ...
                                               "--exit", exit_code  // ...
                                               )};

        {
          exec_path_args::states state;
          REQUIRE_NOTHROW(state = cmd.update_and_get_state());
          REQUIRE_EQ(state.previous, exec_path_args::state::ready);
          REQUIRE_EQ(state.current, exec_path_args::state::running);
          REQUIRE(cmd.manages_process());
        }

        // using the `ips` "unconventionally" ... watch out for the last
        // `notify`:
        REQUIRE(my_sem->wait(default_wait_timeout_ms));

        { // out 1
          std::string const expected{std::string{out1} + '\n'};

          REQUIRE_EQ(cmd.read_stdout(false), expected);
          // consumed but available:
          REQUIRE_EQ(cmd.read_stdout(true), expected);
          REQUIRE_EQ(cmd.read_stdout(false), "");

          // transferred ownership:
          REQUIRE_EQ(cmd.get_stdout(), expected);
          // really consumed:
          REQUIRE_EQ(cmd.read_stdout(true), "");
          REQUIRE_EQ(cmd.get_stdout(), "");
        }

        REQUIRE(my_sem->notify_and_wait(default_wait_timeout_ms));

        { // err 1
          std::string const expected{std::string{err1} + '\n'};

          REQUIRE_EQ(cmd.read_stderr(false), expected);
          // consumed but available:
          REQUIRE_EQ(cmd.read_stderr(true), expected);
          REQUIRE_EQ(cmd.read_stderr(false), "");

          // transferred ownership:
          REQUIRE_EQ(cmd.get_stderr(), expected);
          // really consumed:
          REQUIRE_EQ(cmd.read_stderr(true), "");
          REQUIRE_EQ(cmd.get_stderr(), "");
        }

        REQUIRE(my_sem->notify_and_wait(default_wait_timeout_ms));

        { // out 2
          std::string const expected{std::string{out2} + '\n'};

          REQUIRE_EQ(cmd.read_stdout(false), expected);
          // consumed but available:
          REQUIRE_EQ(cmd.read_stdout(true), expected);
          REQUIRE_EQ(cmd.read_stdout(false), "");

          // transferred ownership:
          REQUIRE_EQ(cmd.get_stdout(), expected);
          // really consumed:
          REQUIRE_EQ(cmd.read_stdout(true), "");
          REQUIRE_EQ(cmd.get_stdout(), "");
        }

        // above mentioned "unconventional" use ...
        REQUIRE_NOTHROW(my_sem->notify());

        {
          exec_path_args::state prev_state;
          REQUIRE_NOTHROW(prev_state = cmd.finish_and_get_prev_state());
          REQUIRE_EQ(prev_state, exec_path_args::state::running);
        }

        { // err 2
          std::string const expected{std::string{err2} + '\n'};

          REQUIRE_EQ(cmd.read_stderr(false), expected);
          // consumed but available:
          REQUIRE_EQ(cmd.read_stderr(true), expected);
          REQUIRE_EQ(cmd.read_stderr(false), "");

          // transferred ownership:
          REQUIRE_EQ(cmd.get_stderr(), expected);
          // really consumed:
          REQUIRE_EQ(cmd.read_stderr(true), "");
          REQUIRE_EQ(cmd.get_stderr(), "");
        }

        REQUIRE_EQ(cmd.read_stderr(true), "");
        REQUIRE_EQ(cmd.read_stdout(true), "");
        REQUIRE_EQ(cmd.get_return_code(), std::stoi(exit_code));
        REQUIRE_LT(0.0, cmd.time_running_ms());
      }
    }
  }
}

} // namespace
} // namespace exec_path_args::os_wrapper

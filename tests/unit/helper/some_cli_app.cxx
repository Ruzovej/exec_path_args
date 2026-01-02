/*
  Copyright 2026 Lukáš Růžička

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

#include <cstdlib>

#include <chrono>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <variant>
#include <vector>

#include <ips/ips.hxx>

namespace {

struct exit_with {
  int code{EXIT_SUCCESS};
};
struct sleep_for_ms {
  int ms;
};
struct echo_stdin_to_stdout {
  int count;
};
struct to_stdout {
  std::string msg;
};
struct to_stderr {
  std::string msg;
};
struct handled_exception {
  inline static std::optional<std::string> msg;
};
struct unhandled_exception {
  inline static std::optional<std::string> msg;
};
struct notify_and_wait {};

using action_variant =
    std::variant<exit_with, sleep_for_ms, echo_stdin_to_stdout, to_stdout,
                 to_stderr, handled_exception, unhandled_exception,
                 notify_and_wait>;

struct input_exception : public std::exception {
  explicit input_exception(std::string &&aMsg) noexcept
      : std::exception{}, msg{std::move(aMsg)} {}

  char const *what() const noexcept override { return msg.c_str(); }

private:
  std::string msg;
};

} // namespace

int main(int const argc, char const **argv) try {
  auto consume_arg = [argc2 = argc - 1, argv2 = argv + 1](
                         bool const require, std::string_view const err_msg =
                                                 "") mutable -> char const * {
    if (0 < argc2) {
      --argc2;
      return *(argv2++);
    } else {
      if (require) {
        throw input_exception{std::string{"Not enough arguments: "} +
                              std::string{err_msg}};
      } else {
        return nullptr;
      }
    }
  };

  std::vector<action_variant> actions;
  std::optional<ips> my_ips;

  char const *arg;
  while ((arg = consume_arg(false)) != nullptr) {
    std::string_view const arg_sv{arg};
    if (arg_sv == "--exit") {
      actions.emplace_back(
          exit_with{std::stoi(consume_arg(true, "missing exit code"))});
    } else if (arg_sv == "--sleep") {
      actions.emplace_back(
          sleep_for_ms{std::stoi(consume_arg(true, "missing sleep duration"))});
    } else if (arg_sv == "--echo") {
      actions.emplace_back(echo_stdin_to_stdout{
          std::stoi(consume_arg(true, "missing echo count"))});
    } else if (arg_sv == "--stdout") {
      actions.emplace_back(
          to_stdout{consume_arg(true, "missing stdout message")});
    } else if (arg_sv == "--stderr") {
      actions.emplace_back(
          to_stderr{consume_arg(true, "missing stderr message")});
    } else if (arg_sv == "--handled-exception") {
      if (handled_exception::msg.has_value()) {
        throw input_exception{"Handled exception message already specified"};
      }
      actions.emplace_back(handled_exception{});
      handled_exception::msg.emplace(
          consume_arg(true, "missing handled exception message"));
    } else if (arg_sv == "--unhandled-exception") {
      if (unhandled_exception::msg.has_value()) {
        throw input_exception{"Unhandled exception message already specified"};
      }
      actions.emplace_back(unhandled_exception{});
      unhandled_exception::msg.emplace(
          consume_arg(true, "missing unhandled exception message"));
    } else if (arg_sv == "--notify-and-wait") {
      // IMHO more isn't needed right now ...
      actions.emplace_back(notify_and_wait{});
    } else if (arg_sv == "--sem-name") {
      if (my_ips.has_value()) {
        throw input_exception{"Semaphore name already specified"};
      }
      my_ips.emplace(consume_arg(true, "missing semaphore name"),
                     false); // non-owning
    } else {
      throw input_exception{std::string{"Unknown argument: "} +
                            std::string{arg_sv}};
    }
  }

  for (auto const &action : actions) {
    std::visit(
        [&my_ips](auto &&arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, exit_with>) {
            my_ips.reset();
            std::exit(arg.code);
          } else if constexpr (std::is_same_v<T, sleep_for_ms>) {
            std::this_thread::sleep_for(std::chrono::milliseconds{arg.ms});
          } else if constexpr (std::is_same_v<T, echo_stdin_to_stdout>) {
            std::string str;
            for (int i{0}; i < arg.count; ++i) {
              std::cin >> str;
              std::cout << str << '\n';
            }
          } else if constexpr (std::is_same_v<T, to_stdout>) {
            std::cout << arg.msg << '\n';
          } else if constexpr (std::is_same_v<T, to_stderr>) {
            std::cerr << arg.msg << '\n';
          } else if constexpr (std::is_same_v<T, handled_exception>) {
            throw std::runtime_error{handled_exception::msg.value()};
          } else if constexpr (std::is_same_v<T, unhandled_exception>) {
            throw unhandled_exception::msg->c_str();
          } else if constexpr (std::is_same_v<T, notify_and_wait>) {
            if (!my_ips.has_value()) {
              throw std::runtime_error{
                  "Semaphore name not specified for sync operation"};
            }

            // flush pending output before notifying:
            std::cout.flush();
            // should be no-op in this case ... but I'm not sure:
            std::cerr.flush();

            if (!my_ips->notify_and_wait(1000)) { // 1 [s]
              throw std::runtime_error{"Timeout while waiting for sync"};
            }
          } else {
            static_assert(!std::is_same_v<T, T>, "non-exhaustive visitor!");
          }
        },
        action);
  }

  return EXIT_SUCCESS;
} catch (input_exception const &e) {
  std::cerr << "some_cli_app caught `input_exception`: " << e.what() << '\n';
  return EXIT_FAILURE;
} catch (std::exception const &e) {
  std::cerr << "some_cli_app caught `std::exception`: " << e.what() << '\n';
  return EXIT_FAILURE;
}
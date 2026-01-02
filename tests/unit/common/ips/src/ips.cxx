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

#include "ips/ips.hxx"

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdexcept>

#define MY_SEM reinterpret_cast<sem_t *>(sem)

namespace detail {

sema_wrap::sema_wrap(std::string const &aName, bool const create)
    : name(create ? aName : ""), owns(create) {
  if (create) {
    // Remove any stale semaphore with this name
    sem_unlink(name.c_str());

    sem = sem_open(name.c_str(), O_CREAT | O_EXCL, S_IRUSR | S_IWUSR,
                   0 // initial value
    );
  } else {
    // Open existing semaphore created by the other process
    sem = sem_open(aName.c_str(), 0);
  }

  if (sem == SEM_FAILED) {
    throw std::runtime_error("sem_open failed: " + std::to_string(errno));
  }
}

sema_wrap::~sema_wrap() noexcept {
  sem_close(MY_SEM);
  if (owns) {
    sem_unlink(name.c_str());
  }
}

// Wait for notification. Returns true if notified, false on timeout.
bool sema_wrap::wait(int const timeout_ms) {
  if (timeout_ms < 0) {
    // Infinite wait
    while (sem_wait(MY_SEM) != 0) {
      if (errno != EINTR) {
        throw std::runtime_error("sem_wait failed");
      }
    }
    return true;
  }

  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    throw std::runtime_error("clock_gettime failed");
  }

  ts.tv_sec += timeout_ms / 1'000;
  ts.tv_nsec += (timeout_ms % 1'000) * 1'000'000L;

  while (1'000'000'000L <= ts.tv_nsec) {
    ts.tv_sec += 1;
    ts.tv_nsec -= 1'000'000'000L;
  }

  while (sem_timedwait(MY_SEM, &ts) != 0) {
    if (errno == ETIMEDOUT) {
      return false;
    }
    if (errno != EINTR) {
      throw std::runtime_error("sem_timedwait failed");
    }
  }
  return true;
}

void sema_wrap::notify() {
  if (sem_post(MY_SEM) != 0) {
    throw std::runtime_error("sem_post failed");
  }
}

} // namespace detail

ips::ips(std::string const &aName, bool const create)
    : sema_wait{aName + (create ? "_wait" : "_notify"), create},
      sema_notify{aName + (create ? "_notify" : "_wait"), create} {}

bool ips::wait(int const timeout_ms) { return sema_wait.wait(timeout_ms); }

void ips::notify() { sema_notify.notify(); }

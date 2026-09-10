// libctru 2.7 creates its APT notification thread at 0x31, below gameplay.
// GPU-heavy frames can leave no idle CPU for HOME notifications. Interpose
// only that creation during aptInit; do not change game/audio/UI priorities.
#include <3ds.h>
#include <stddef.h>

Result __real_aptInit(void);
Thread __real_threadCreate(ThreadFunc entrypoint, void *arg, size_t stack_size,
                          int priority, int core_id, bool detached);
static void *apt_init_tls;
static int apt_event_priority;

Result __wrap_aptInit(void) {
  void *previous = __atomic_exchange_n(&apt_init_tls, getThreadLocalStorage(), __ATOMIC_ACQ_REL);
  Result result = __real_aptInit();
  __atomic_store_n(&apt_init_tls, previous, __ATOMIC_RELEASE);
  return result;
}

Thread __wrap_threadCreate(ThreadFunc entrypoint, void *arg, size_t stack_size,
                          int priority, int core_id, bool detached) {
  // Match libctru's APT handler signature on the initializing thread. All
  // other threadCreate calls, including identical calls after init, pass through.
  bool apt_handler = __atomic_load_n(&apt_init_tls, __ATOMIC_ACQUIRE) == getThreadLocalStorage() &&
                     stack_size == 0x1000 && priority == 0x31 && core_id == -2 && detached;
  int selected_priority = apt_handler ? 0x19 : priority;
  Thread thread = __real_threadCreate(entrypoint, arg, stack_size, selected_priority, core_id, detached);
  if (apt_handler && !thread) {
    selected_priority = priority;
    thread = __real_threadCreate(entrypoint, arg, stack_size, priority, core_id, detached);
  }
  if (apt_handler && thread) apt_event_priority = selected_priority;
  return thread;
}

int Platform3DS_GetAptEventPriority(void) { return apt_event_priority; }

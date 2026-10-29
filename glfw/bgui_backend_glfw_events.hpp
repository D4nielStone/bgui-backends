#pragma once
#include <functional>

namespace bgui {
using glfw_event_observer = std::function<void()>;
// Observe native protocols before GLFW consumes their events. Returns the prior
// observer so a scoped application loop can restore it without losing callers.
glfw_event_observer set_glfw_event_observer(glfw_event_observer observer);
}

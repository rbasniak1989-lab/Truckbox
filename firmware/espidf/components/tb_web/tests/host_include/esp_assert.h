/* Host-only adapter for compiling the policy against real ESP-IDF format headers. */
#pragma once
#define ESP_STATIC_ASSERT(condition, message) _Static_assert(condition, message)

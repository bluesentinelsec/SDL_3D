/**
 * @file signal_bus.h
 * @brief Decoupled event dispatch for gameplay mechanics.
 *
 * The signal bus is the wiring layer between triggers and actions. Any
 * system can emit a signal, any system can listen. Signals are integer
 * IDs defined by the caller (typically an enum). Payloads are optional
 * property bags (sdl3d_properties) for a uniform, inspectable format.
 *
 * Handlers fire synchronously during sdl3d_signal_emit, in the order
 * they were connected. There is no deferred queue — if deferred
 * dispatch is needed, layer it on top via the timer system (Phase 5).
 *
 * Usage:
 * @code
 *   enum { SIG_ALARM = 1, SIG_DOOR_UNLOCK = 2 };
 *
 *   void on_alarm(void *ud, int sig, const sdl3d_properties *payload) {
 *       printf("Alarm triggered!\n");
 *   }
 *
 *   sdl3d_signal_bus *bus = sdl3d_signal_bus_create();
 *   int conn = sdl3d_signal_connect(bus, SIG_ALARM, on_alarm, NULL);
 *   sdl3d_signal_emit(bus, SIG_ALARM, NULL);  // fires on_alarm
 *   sdl3d_signal_disconnect(bus, conn);
 *   sdl3d_signal_bus_destroy(bus);
 * @endcode
 */

#ifndef SDL3D_SIGNAL_BUS_H
#define SDL3D_SIGNAL_BUS_H

#include <stdbool.h>

#include "sdl3d/properties.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque signal bus handle. */
    typedef struct sdl3d_signal_bus sdl3d_signal_bus;

    /**
     * @brief Signal handler callback.
     *
     * @param userdata  Caller-provided context pointer from sdl3d_signal_connect.
     * @param signal_id The signal that was emitted.
     * @param payload   Optional property bag attached to the emission, or NULL.
     *                  The payload is valid only for the duration of the call.
     */
    typedef void (*sdl3d_signal_handler)(void *userdata, int signal_id, const sdl3d_properties *payload);

    /* ================================================================== */
    /* Lifecycle                                                          */
    /* ================================================================== */

    /**
     * @brief Create an empty signal bus.
     * @return A new signal bus, or NULL on allocation failure.
     */
    sdl3d_signal_bus *sdl3d_signal_bus_create(void);

    /**
     * @brief Destroy a signal bus and free all connections.
     *
     * Any pending emissions in progress (e.g., a handler that destroys
     * the bus) result in undefined behavior. Safe to call with NULL.
     */
    void sdl3d_signal_bus_destroy(sdl3d_signal_bus *bus);

    /* ================================================================== */
    /* Connection management                                              */
    /* ================================================================== */

    /**
     * @brief Connect a handler to a signal.
     *
     * The handler will be called each time the signal is emitted, in the
     * order connections were made. Multiple handlers can be connected to
     * the same signal. The same handler can be connected multiple times
     * (each connection fires independently).
     *
     * @param bus       The signal bus.
     * @param signal_id Caller-defined signal identifier (any integer).
     * @param handler   Callback to invoke on emission. Must not be NULL.
     * @param userdata  Opaque pointer passed to the handler. May be NULL.
     * @return A connection ID (>= 1) for use with sdl3d_signal_disconnect,
     *         or 0 on failure.
     */
    int sdl3d_signal_connect(sdl3d_signal_bus *bus, int signal_id, sdl3d_signal_handler handler, void *userdata);

    /**
     * @brief Disconnect a handler by connection ID.
     *
     * The connection ID is the value returned by sdl3d_signal_connect.
     * After disconnection, the handler will not be called on future
     * emissions. Safe to call during an emission (the disconnected
     * handler is skipped for the remainder of the current emission).
     * No-op if the ID is invalid or already disconnected.
     */
    void sdl3d_signal_disconnect(sdl3d_signal_bus *bus, int connection_id);

    /**
     * @brief Disconnect all handlers for a specific signal.
     *
     * All connections listening to signal_id are removed.
     */
    void sdl3d_signal_disconnect_all(sdl3d_signal_bus *bus, int signal_id);

    /* ================================================================== */
    /* Emission                                                           */
    /* ================================================================== */

    /**
     * @brief Emit a signal, invoking all connected handlers synchronously.
     *
     * Handlers are called in connection order. The payload (if non-NULL)
     * is passed by const pointer and is valid only for the duration of
     * each handler call. Handlers may emit other signals (reentrant),
     * connect new handlers, or disconnect themselves.
     *
     * @param bus       The signal bus.
     * @param signal_id The signal to emit.
     * @param payload   Optional property bag, or NULL.
     */
    void sdl3d_signal_emit(sdl3d_signal_bus *bus, int signal_id, const sdl3d_properties *payload);

    /* ================================================================== */
    /* Query                                                              */
    /* ================================================================== */

    /**
     * @brief Get the number of active connections on the bus.
     *
     * Disconnected (dead) connections are not counted.
     */
    int sdl3d_signal_bus_connection_count(const sdl3d_signal_bus *bus);

#ifdef __cplusplus
}
#endif

#endif /* SDL3D_SIGNAL_BUS_H */

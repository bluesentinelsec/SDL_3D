/**
 * @file signal_bus.h
 * @brief Decoupled event dispatch for gameplay mechanics.
 *
 * The signal bus is the wiring layer between triggers and actions. Any
 * system can emit a signal, any system can listen. Signals are integer
 * IDs defined by the caller (typically an enum). Payloads are optional
 * property bags (slayer3d_properties) for a uniform, inspectable format.
 *
 * Handlers fire synchronously during slayer3d_signal_emit, in the order
 * they were connected. There is no deferred queue — if deferred
 * dispatch is needed, layer it on top via the timer system (Phase 5).
 *
 * Usage:
 * @code
 *   enum { SIG_ALARM = 1, SIG_DOOR_UNLOCK = 2 };
 *
 *   void on_alarm(void *ud, int sig, const slayer3d_properties *payload) {
 *       printf("Alarm triggered!\n");
 *   }
 *
 *   slayer3d_signal_bus *bus = slayer3d_signal_bus_create();
 *   int conn = slayer3d_signal_connect(bus, SIG_ALARM, on_alarm, NULL);
 *   slayer3d_signal_emit(bus, SIG_ALARM, NULL);  // fires on_alarm
 *   slayer3d_signal_disconnect(bus, conn);
 *   slayer3d_signal_bus_destroy(bus);
 * @endcode
 */

#ifndef SLAYER3D_SIGNAL_BUS_H
#define SLAYER3D_SIGNAL_BUS_H

#include <stdbool.h>

#include "slayer3d/properties.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque signal bus handle. */
    typedef struct slayer3d_signal_bus slayer3d_signal_bus;

    /**
     * @brief Signal handler callback.
     *
     * @param userdata  Caller-provided context pointer from slayer3d_signal_connect.
     * @param signal_id The signal that was emitted.
     * @param payload   Optional property bag attached to the emission, or NULL.
     *                  The payload is valid only for the duration of the call.
     */
    typedef void (*slayer3d_signal_handler)(void *userdata, int signal_id, const slayer3d_properties *payload);

    /* ================================================================== */
    /* Lifecycle                                                          */
    /* ================================================================== */

    /**
     * @brief Create an empty signal bus.
     * @return A new signal bus, or NULL on allocation failure.
     */
    slayer3d_signal_bus *slayer3d_signal_bus_create(void);

    /**
     * @brief Destroy a signal bus and free all connections.
     *
     * Any pending emissions in progress (e.g., a handler that destroys
     * the bus) result in undefined behavior. Safe to call with NULL.
     */
    void slayer3d_signal_bus_destroy(slayer3d_signal_bus *bus);

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
     * @return A connection ID (>= 1) for use with slayer3d_signal_disconnect,
     *         or 0 on failure.
     */
    int slayer3d_signal_connect(slayer3d_signal_bus *bus, int signal_id, slayer3d_signal_handler handler,
                                void *userdata);

    /**
     * @brief Disconnect a handler by connection ID.
     *
     * The connection ID is the value returned by slayer3d_signal_connect.
     * After disconnection, the handler will not be called on future
     * emissions. Safe to call during an emission (the disconnected
     * handler is skipped for the remainder of the current emission).
     * No-op if the ID is invalid or already disconnected.
     */
    void slayer3d_signal_disconnect(slayer3d_signal_bus *bus, int connection_id);

    /**
     * @brief Disconnect all handlers for a specific signal.
     *
     * All connections listening to signal_id are removed.
     */
    void slayer3d_signal_disconnect_all(slayer3d_signal_bus *bus, int signal_id);

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
    void slayer3d_signal_emit(slayer3d_signal_bus *bus, int signal_id, const slayer3d_properties *payload);

    /* ================================================================== */
    /* Query                                                              */
    /* ================================================================== */

    /**
     * @brief Get the number of active connections on the bus.
     *
     * Disconnected (dead) connections are not counted.
     */
    int slayer3d_signal_bus_connection_count(const slayer3d_signal_bus *bus);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_SIGNAL_BUS_H */

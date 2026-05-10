/**
 * @file signal_bus.c
 * @brief Signal bus implementation — flat dynamic array of connections.
 *
 * Connections are stored in a single flat array. Each connection has a
 * unique monotonic ID, a signal_id it listens to, a handler, userdata,
 * and an alive flag. Disconnection marks the connection as dead rather
 * than removing it, so iteration during emission is safe. Dead entries
 * are compacted lazily when not inside an emission.
 */

#include "slayer3d/signal_bus.h"

#include <SDL3/SDL_stdinc.h>

/* ================================================================== */
/* Internal types                                                     */
/* ================================================================== */

typedef struct connection
{
    int id;
    int signal_id;
    slayer3d_signal_handler handler;
    void *userdata;
    bool alive;
} connection;

struct slayer3d_signal_bus
{
    connection *connections;
    int count;
    int capacity;
    int next_id;    /* Monotonically increasing connection ID. */
    int emit_depth; /* Reentrancy depth. >0 means we are inside emit. */
    int dead_count; /* Number of dead connections awaiting compaction. */
};

/* ================================================================== */
/* Internal helpers                                                   */
/* ================================================================== */

/**
 * Remove dead connections by compacting the array. Only safe when
 * emit_depth == 0 (not inside any emission).
 */
static void compact(slayer3d_signal_bus *bus)
{
    if (bus->dead_count == 0)
        return;

    int write = 0;
    for (int read = 0; read < bus->count; ++read)
    {
        if (bus->connections[read].alive)
        {
            if (write != read)
                bus->connections[write] = bus->connections[read];
            write++;
        }
    }
    bus->count = write;
    bus->dead_count = 0;
}

static bool ensure_capacity(slayer3d_signal_bus *bus)
{
    if (bus->count < bus->capacity)
        return true;

    int new_cap = bus->capacity < 8 ? 8 : bus->capacity * 2;
    connection *new_buf = (connection *)SDL_realloc(bus->connections, (size_t)new_cap * sizeof(connection));
    if (new_buf == NULL)
        return false;
    bus->connections = new_buf;
    bus->capacity = new_cap;
    return true;
}

/* ================================================================== */
/* Lifecycle                                                          */
/* ================================================================== */

slayer3d_signal_bus *slayer3d_signal_bus_create(void)
{
    slayer3d_signal_bus *bus = (slayer3d_signal_bus *)SDL_calloc(1, sizeof(slayer3d_signal_bus));
    if (bus != NULL)
        bus->next_id = 1;
    return bus;
}

void slayer3d_signal_bus_destroy(slayer3d_signal_bus *bus)
{
    if (bus == NULL)
        return;
    SDL_free(bus->connections);
    SDL_free(bus);
}

/* ================================================================== */
/* Connection management                                              */
/* ================================================================== */

int slayer3d_signal_connect(slayer3d_signal_bus *bus, int signal_id, slayer3d_signal_handler handler, void *userdata)
{
    if (bus == NULL || handler == NULL)
        return 0;

    /* Compact dead entries before growing, but only outside emission. */
    if (bus->emit_depth == 0 && bus->dead_count > 0)
        compact(bus);

    if (!ensure_capacity(bus))
        return 0;

    int id = bus->next_id++;
    connection *c = &bus->connections[bus->count++];
    c->id = id;
    c->signal_id = signal_id;
    c->handler = handler;
    c->userdata = userdata;
    c->alive = true;
    return id;
}

void slayer3d_signal_disconnect(slayer3d_signal_bus *bus, int connection_id)
{
    if (bus == NULL || connection_id <= 0)
        return;

    for (int i = 0; i < bus->count; ++i)
    {
        connection *c = &bus->connections[i];
        if (c->alive && c->id == connection_id)
        {
            c->alive = false;
            bus->dead_count++;
            break;
        }
    }

    if (bus->emit_depth == 0 && bus->dead_count > 0)
        compact(bus);
}

void slayer3d_signal_disconnect_all(slayer3d_signal_bus *bus, int signal_id)
{
    if (bus == NULL)
        return;

    for (int i = 0; i < bus->count; ++i)
    {
        connection *c = &bus->connections[i];
        if (c->alive && c->signal_id == signal_id)
        {
            c->alive = false;
            bus->dead_count++;
        }
    }

    if (bus->emit_depth == 0 && bus->dead_count > 0)
        compact(bus);
}

/* ================================================================== */
/* Emission                                                           */
/* ================================================================== */

void slayer3d_signal_emit(slayer3d_signal_bus *bus, int signal_id, const slayer3d_properties *payload)
{
    if (bus == NULL)
        return;

    bus->emit_depth++;

    /*
     * Snapshot the count before iterating. Handlers may connect new
     * handlers (appended beyond current count), which will not fire
     * during this emission — preventing infinite loops from a handler
     * that connects itself.
     */
    int snapshot = bus->count;
    for (int i = 0; i < snapshot; ++i)
    {
        const connection *c = &bus->connections[i];
        if (c->alive && c->signal_id == signal_id)
            c->handler(c->userdata, signal_id, payload);
    }

    bus->emit_depth--;

    if (bus->emit_depth == 0 && bus->dead_count > 0)
        compact(bus);
}

/* ================================================================== */
/* Query                                                              */
/* ================================================================== */

int slayer3d_signal_bus_connection_count(const slayer3d_signal_bus *bus)
{
    if (bus == NULL)
        return 0;
    return bus->count - bus->dead_count;
}

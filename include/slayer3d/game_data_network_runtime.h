/**
 * @file game_data_network_runtime.h
 * @brief Public network runtime APIs for JSON-authored game data.
 */

#ifndef SLAYER3D_GAME_DATA_NETWORK_RUNTIME_H
#define SLAYER3D_GAME_DATA_NETWORK_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>

#include <SDL3/SDL_stdinc.h>

#include "slayer3d/game.h"
#include "slayer3d/game_data_app.h"
#include "slayer3d/game_data_network.h"
#include "slayer3d/input.h"
#include "slayer3d/network.h"
#include "slayer3d/network_replication.h"
#include "slayer3d/properties.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Opaque runtime created from one game JSON document. */
    typedef struct slayer3d_game_data_runtime slayer3d_game_data_runtime;

    /**
     * @brief Return whether the game data authored a network replication schema.
     *
     * Local-only games can omit the `network` block entirely. In that case
     * there is no schema hash to exchange during multiplayer handshakes.
     */
    bool slayer3d_game_data_has_network_schema(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Copy the deterministic network replication schema hash.
     *
     * The hash covers the authored protocol, replication channels, fields,
     * inputs, and control messages, but ignores unrelated game data. It is
     * intended for host/client compatibility checks before gameplay begins.
     *
     * @return true when @p runtime has an authored network schema and
     * @p out_hash was filled with SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE bytes.
     */
    bool slayer3d_game_data_get_network_schema_hash(const slayer3d_game_data_runtime *runtime,
                                                    Uint8 out_hash[SLAYER3D_REPLICATION_SCHEMA_HASH_SIZE]);

    /**
     * @brief Resolve an authored scene-state key used by network UI/session flows.
     *
     * Games may place arbitrary string key maps under `network.scene_state`.
     * Host integration code can use this helper to avoid hard-coding the
     * `scene_state` property names that authored lobby, discovery, or direct
     * connect scenes display.
     *
     * For example, `network.scene_state.host.status` can resolve to
     * `multiplayer_host_status`.
     *
     * @param runtime Loaded game data runtime.
     * @param scope Authored scene-state group, such as `host`.
     * @param name Authored key name within the group, such as `status`.
     * @param out_key Receives a string owned by @p runtime.
     * @return true when the key exists and @p out_key was filled.
     */
    bool slayer3d_game_data_get_network_scene_state_key(const slayer3d_game_data_runtime *runtime, const char *scope,
                                                        const char *name, const char **out_key);

    /**
     * @brief Resolve an authored network session scene id.
     *
     * Games may author reusable scene ids under `network.session_flow.scenes`,
     * such as `play`, `host_lobby`, `join`, `direct_connect`, or `title`.
     * Host integration code can use this helper to avoid hard-coding scene ids
     * while still owning transport/session objects.
     *
     * @param runtime Loaded game data runtime.
     * @param name Authored scene semantic name.
     * @param out_scene Receives a scene id owned by @p runtime.
     * @return true when the scene semantic exists and @p out_scene was filled.
     */
    bool slayer3d_game_data_get_network_session_scene(const slayer3d_game_data_runtime *runtime, const char *name,
                                                      const char **out_scene);

    /**
     * @brief Resolve an authored network session scene-state key.
     *
     * Keys are authored under `network.session_flow.state_keys`, such as
     * `match_mode`, `network_role`, or `network_flow`.
     *
     * @param runtime Loaded game data runtime.
     * @param name Authored key semantic name.
     * @param out_key Receives a scene-state key owned by @p runtime.
     * @return true when the key semantic exists and @p out_key was filled.
     */
    bool slayer3d_game_data_get_network_session_state_key(const slayer3d_game_data_runtime *runtime, const char *name,
                                                          const char **out_key);

    /**
     * @brief Resolve an authored network session scene-state value.
     *
     * Values are authored under `network.session_flow.state_values.<group>`.
     * For example, `state_values.network_role.host` may resolve to `host`.
     *
     * @param runtime Loaded game data runtime.
     * @param group Authored value group, usually a state key semantic.
     * @param name Authored value semantic name.
     * @param out_value Receives a value string owned by @p runtime.
     * @return true when the value semantic exists and @p out_value was filled.
     */
    bool slayer3d_game_data_get_network_session_state_value(const slayer3d_game_data_runtime *runtime,
                                                            const char *group, const char *name,
                                                            const char **out_value);

    /**
     * @brief Resolve an authored network session message.
     *
     * Messages are authored under `network.session_flow.messages.<group>`.
     * Host integration code can use this for player-facing network flow text
     * such as disconnect reasons or termination prompts without hard-coding
     * those strings in C.
     *
     * @param runtime Loaded game data runtime.
     * @param group Authored message group.
     * @param name Authored message semantic name.
     * @param out_message Receives a message string owned by @p runtime.
     * @return true when the message semantic exists and @p out_message was filled.
     */
    bool slayer3d_game_data_get_network_session_message(const slayer3d_game_data_runtime *runtime, const char *group,
                                                        const char *name, const char **out_message);

    /**
     * @brief Return whether authored managed network orchestration is enabled.
     *
     * Managed networking is enabled by `network.session_flow.managed_runtime.enabled`.
     * Hosts may still choose not to run it; this helper only reports the
     * authored game-data policy.
     *
     * @param runtime Loaded game data runtime.
     * @return true when the game authors managed runtime networking as enabled.
     */
    bool slayer3d_game_data_network_managed_runtime_enabled(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Resolve the authored delay before acknowledging a terminated network match.
     *
     * The value is authored at
     * `network.session_flow.managed_runtime.termination_ack_delay_seconds`.
     *
     * @param runtime Loaded game data runtime.
     * @param out_seconds Receives the non-negative delay in seconds.
     * @return true when the delay is authored and @p out_seconds was filled.
     */
    bool slayer3d_game_data_get_network_managed_termination_ack_delay(const slayer3d_game_data_runtime *runtime,
                                                                      float *out_seconds);

    /**
     * @brief Test whether a scene keeps a managed network session alive.
     *
     * Scene semantics are authored under
     * `network.session_flow.managed_runtime.keep_alive_scenes.<session>`.
     * Each entry references a semantic from `network.session_flow.scenes`; this
     * helper resolves those semantics and compares them with @p scene_name.
     *
     * @param runtime Loaded game data runtime.
     * @param session_name Managed session semantic, such as `host` or
     * `direct_connect`.
     * @param scene_name Concrete active scene id to test.
     * @return true when the session should stay alive in the scene.
     */
    bool slayer3d_game_data_network_managed_keep_alive_scene_matches(const slayer3d_game_data_runtime *runtime,
                                                                     const char *session_name, const char *scene_name);

    /**
     * @brief Execute an authored network session-flow event.
     *
     * Events are authored under `network.session_flow.events.<name>`. An event
     * may be either an action array or an object with optional `pause` and
     * `actions` fields. The action array uses the same data action vocabulary
     * as logic bindings and scene activity actions. String values in supported
     * actions can reference string payload fields with `{field}` placeholders.
     *
     * @param runtime Loaded game data runtime.
     * @param ctx Optional game context; required only when the event authors a
     * `pause` field.
     * @param name Authored event semantic name.
     * @param payload Optional payload passed to event actions.
     * @param error_buffer Optional buffer for a failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the event exists and all actions execute.
     */
    bool slayer3d_game_data_run_network_session_flow_event(slayer3d_game_data_runtime *runtime,
                                                           slayer3d_game_context *ctx, const char *name,
                                                           const slayer3d_properties *payload, char *error_buffer,
                                                           int error_buffer_size);

    /**
     * @brief Resolve an authored network runtime replication channel binding.
     *
     * Runtime bindings are authored under `network.runtime_bindings.replication`
     * and map host integration semantics, such as `state_snapshot` or
     * `client_input`, to concrete replication channel names declared in
     * `network.replication`.
     *
     * @param runtime Loaded game data runtime.
     * @param name Authored binding semantic name.
     * @param out_channel Receives a replication channel name owned by @p runtime.
     * @return true when the binding exists and @p out_channel was filled.
     */
    bool slayer3d_game_data_get_network_runtime_replication(const slayer3d_game_data_runtime *runtime, const char *name,
                                                            const char **out_channel);

    /**
     * @brief Resolve an authored network runtime control-message binding.
     *
     * Runtime bindings are authored under `network.runtime_bindings.controls`
     * and map host integration semantics, such as `pause_request` or
     * `disconnect`, to concrete control-message names declared in
     * `network.control_messages`.
     *
     * @param runtime Loaded game data runtime.
     * @param name Authored binding semantic name.
     * @param out_control Receives a control-message name owned by @p runtime.
     * @return true when the binding exists and @p out_control was filled.
     */
    bool slayer3d_game_data_get_network_runtime_control(const slayer3d_game_data_runtime *runtime, const char *name,
                                                        const char **out_control);

    /**
     * @brief Resolve the semantic runtime binding for an authored control message.
     *
     * This is the reverse lookup for
     * @ref slayer3d_game_data_get_network_runtime_control. It lets generic
     * network loops decode a control packet and dispatch on the authored
     * runtime semantic, such as `pause_request` or `disconnect`, without
     * knowing the concrete control-message name in the wire schema.
     *
     * @param runtime Loaded game data runtime.
     * @param control_name Authored control message name from `network.control_messages`.
     * @param out_binding Receives the semantic binding name owned by @p runtime.
     * @return true when a runtime control binding maps to @p control_name.
     */
    bool slayer3d_game_data_get_network_runtime_control_binding(const slayer3d_game_data_runtime *runtime,
                                                                const char *control_name, const char **out_binding);

    /**
     * @brief Resolve an authored network runtime input action binding.
     *
     * Runtime action bindings are authored under
     * `network.runtime_bindings.actions` and map host integration semantics,
     * such as `menu_back` or `camera_toggle`, to concrete input action names.
     *
     * @param runtime Loaded game data runtime.
     * @param name Authored binding semantic name.
     * @param out_action Receives the resolved action id.
     * @return true when the binding exists and resolves to an input action.
     */
    bool slayer3d_game_data_get_network_runtime_action(const slayer3d_game_data_runtime *runtime, const char *name,
                                                       int *out_action);

    /**
     * @brief Resolve an authored network runtime signal binding.
     *
     * Runtime signal bindings are authored under
     * `network.runtime_bindings.signals` and map host integration semantics,
     * such as `lobby_start` or `ui_select`, to concrete signal names.
     *
     * @param runtime Loaded game data runtime.
     * @param name Authored binding semantic name.
     * @param out_signal Receives the resolved signal id.
     * @return true when the binding exists and resolves to a signal.
     */
    bool slayer3d_game_data_get_network_runtime_signal(const slayer3d_game_data_runtime *runtime, const char *name,
                                                       int *out_signal);

    /**
     * @brief Return the number of authored haptics policies.
     *
     * Policies are authored under `haptics.policies`.
     *
     * @param runtime Loaded game data runtime.
     * @return Number of authored policies, or 0 when none are authored.
     */
    int slayer3d_game_data_haptics_policy_count(const slayer3d_game_data_runtime *runtime);

    /**
     * @brief Read an authored haptics policy descriptor by index.
     *
     * This exposes the policy's signal and rumble parameters so hosts can
     * subscribe to the necessary signals. Use
     * `slayer3d_game_data_match_haptics_policy()` before playing rumble.
     *
     * @param runtime Loaded game data runtime.
     * @param index Zero-based policy index.
     * @param out_policy Receives the policy descriptor.
     * @return true when @p index exists and @p out_policy was filled.
     */
    bool slayer3d_game_data_get_haptics_policy_at(const slayer3d_game_data_runtime *runtime, int index,
                                                  slayer3d_game_data_haptics_policy *out_policy);

    /**
     * @brief Test whether an authored haptics policy matches a signal event.
     *
     * The helper checks the policy signal, optional `enabled_if` condition, and
     * optional payload actor filters. Payload actor filters read actor names
     * from the signal payload and may match by concrete actor name or authored
     * entity tags.
     *
     * @param runtime Loaded game data runtime.
     * @param index Zero-based policy index.
     * @param signal_id Emitted signal id.
     * @param payload Optional signal payload.
     * @param out_policy Receives the matching policy descriptor.
     * @return true when the policy exists and matches this event.
     */
    bool slayer3d_game_data_match_haptics_policy(const slayer3d_game_data_runtime *runtime, int index, int signal_id,
                                                 const slayer3d_properties *payload,
                                                 slayer3d_game_data_haptics_policy *out_policy);

    /**
     * @brief Resolve the authored network pause input action id.
     *
     * The pause binding is authored under `network.runtime_bindings.pause`.
     * The `action` value references an input action that should request
     * pause/resume in network play.
     *
     * @param runtime Loaded game data runtime.
     * @param out_action_id Receives the runtime input action id.
     * @return true when the pause action binding exists and resolves.
     */
    bool slayer3d_game_data_get_network_runtime_pause_action(const slayer3d_game_data_runtime *runtime,
                                                             int *out_action_id);

    /**
     * @brief Read the authored network pause state property.
     *
     * The pause state is authored under `network.runtime_bindings.pause.state`
     * as an actor reference and bool property name. This lets host code mirror
     * pause state into replicated game state without knowing concrete actor ids
     * or property keys.
     *
     * @param runtime Loaded game data runtime.
     * @param out_paused Receives the current pause state.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the pause state binding exists and resolves to a bool.
     */
    bool slayer3d_game_data_get_network_runtime_pause_state(const slayer3d_game_data_runtime *runtime, bool *out_paused,
                                                            char *error_buffer, int error_buffer_size);

    /**
     * @brief Write the authored network pause state property.
     *
     * The pause state is authored under `network.runtime_bindings.pause.state`
     * as an actor reference and bool property name. This helper sets that
     * property to @p paused.
     *
     * @param runtime Loaded game data runtime.
     * @param paused New pause state.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the pause state binding exists and was written.
     */
    bool slayer3d_game_data_set_network_runtime_pause_state(slayer3d_game_data_runtime *runtime, bool paused,
                                                            char *error_buffer, int error_buffer_size);

    /**
     * @brief Format a diagnostic summary for an authored network snapshot channel.
     *
     * The helper walks the named host-to-client replication channel in authored
     * schema order and appends every replicated actor field to @p buffer. It
     * also includes the active scene and authored `network.session_flow`
     * scene-state key values when present. This is intended for lightweight
     * host/client diagnostics without hard-coding game actor ids or property
     * paths in the host program.
     *
     * @param runtime Runtime whose current state should be described.
     * @param replication_name Authored host-to-client replication channel name.
     * @param tick Simulation or packet tick to include in the description.
     * @param buffer Destination text buffer.
     * @param buffer_size Destination text buffer size in bytes.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when @p buffer contains a complete, null-terminated summary.
     */
    bool slayer3d_game_data_describe_network_snapshot(const slayer3d_game_data_runtime *runtime,
                                                      const char *replication_name, Uint32 tick, char *buffer,
                                                      size_t buffer_size, char *error_buffer, int error_buffer_size);

    /**
     * @brief Emit an authored network snapshot diagnostic when policy allows.
     *
     * The named policy is read from `network.diagnostics.snapshots[]`. The
     * policy chooses the replicated channel to describe, enabled state, log
     * level, cadence, session-state inclusion, and message template. Template
     * placeholders can reference `{name}`, `{event}`, `{extra}`, `{tick}`, and
     * `{description}`.
     *
     * If the policy is disabled or cadence suppresses the message, the function
     * returns true with @p out_logged set to false.
     *
     * @param runtime Runtime whose current state should be logged.
     * @param diagnostic_name Authored diagnostic policy name.
     * @param tick Simulation or packet tick to include in the description.
     * @param event Optional event label, such as `host_snapshot_sent`.
     * @param extra Optional caller-provided context string.
     * @param out_logged Optional flag set true only when a log line is emitted.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the policy was handled successfully.
     */
    bool slayer3d_game_data_log_network_snapshot_diagnostic(slayer3d_game_data_runtime *runtime,
                                                            const char *diagnostic_name, Uint32 tick, const char *event,
                                                            const char *extra, bool *out_logged, char *error_buffer,
                                                            int error_buffer_size);

    /**
     * @brief Encode an authored host-to-client replication snapshot.
     *
     * The named replication channel must exist in the loaded `network`
     * schema and have `direction: "host_to_client"`. The packet includes a
     * deterministic header, schema hash, channel index, tick, typed field
     * tags, and field values in authored schema order. Callers provide the
     * destination buffer; no allocation is performed.
     *
     * @param runtime Runtime whose actor state should be serialized.
     * @param replication_name Authored replication channel name.
     * @param tick Authoritative simulation tick to include in the snapshot.
     * @param buffer Destination packet buffer.
     * @param buffer_size Destination buffer size in bytes.
     * @param out_size Receives written packet size in bytes.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the full snapshot was encoded.
     */
    bool slayer3d_game_data_encode_network_snapshot(const slayer3d_game_data_runtime *runtime,
                                                    const char *replication_name, Uint32 tick, void *buffer,
                                                    size_t buffer_size, size_t *out_size, char *error_buffer,
                                                    int error_buffer_size);

    /**
     * @brief Encode a host-to-client snapshot packet by runtime binding semantic.
     *
     * @p binding_name is resolved through `network.runtime_bindings.replication`
     * before encoding. Use this from generic session/runtime code so the loop
     * does not need to know concrete replication channel names.
     */
    bool slayer3d_game_data_encode_network_runtime_snapshot(const slayer3d_game_data_runtime *runtime,
                                                            const char *binding_name, Uint32 tick, void *buffer,
                                                            size_t buffer_size, size_t *out_size, char *error_buffer,
                                                            int error_buffer_size);

    /**
     * @brief Encode and send a host-to-client snapshot packet by runtime binding.
     */
    bool slayer3d_game_data_send_network_runtime_snapshot(const slayer3d_game_data_runtime *runtime,
                                                          slayer3d_network_session *session, const char *binding_name,
                                                          Uint32 tick, char *error_buffer, int error_buffer_size);

    /**
     * @brief Decode and apply an authored host-to-client replication snapshot.
     *
     * The packet must match the runtime schema hash and reference a
     * host-to-client channel in the loaded `network` schema. Field tags and
     * payload sizes are checked strictly. On failure, the function returns
     * false and reports a best-effort error; callers should discard the
     * packet. Successfully decoded values are applied directly to actors.
     *
     * @param runtime Runtime whose actor state should be updated.
     * @param packet Source packet buffer.
     * @param packet_size Source packet size in bytes.
     * @param out_tick Receives the authoritative snapshot tick, if non-NULL.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the snapshot was decoded and applied completely.
     */
    bool slayer3d_game_data_apply_network_snapshot(slayer3d_game_data_runtime *runtime, const void *packet,
                                                   size_t packet_size, Uint32 *out_tick, char *error_buffer,
                                                   int error_buffer_size);

    /**
     * @brief Decode and apply a host-to-client snapshot expected by runtime binding.
     *
     * The packet must be a valid snapshot packet for the concrete replication
     * channel mapped by @p binding_name. This prevents generic session loops
     * from accidentally applying a valid but unexpected channel.
     */
    bool slayer3d_game_data_apply_network_runtime_snapshot(slayer3d_game_data_runtime *runtime,
                                                           const char *binding_name, const void *packet,
                                                           size_t packet_size, Uint32 *out_tick, char *error_buffer,
                                                           int error_buffer_size);

    /**
     * @brief Encode an authored client-to-host input replication packet.
     *
     * The named replication channel must exist in the loaded `network`
     * schema and have `direction: "client_to_host"`. Each authored input
     * action is sampled from @p input as a float value and written in schema
     * order with strict field type tags. Callers provide the destination
     * buffer; no allocation is performed.
     *
     * @param runtime Runtime containing the authored network schema and actions.
     * @param replication_name Authored replication channel name.
     * @param input Input manager whose current snapshot should be serialized.
     * @param tick Client simulation/input tick to include in the packet.
     * @param buffer Destination packet buffer.
     * @param buffer_size Destination buffer size in bytes.
     * @param out_size Receives written packet size in bytes.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the full input packet was encoded.
     */
    bool slayer3d_game_data_encode_network_input(const slayer3d_game_data_runtime *runtime,
                                                 const char *replication_name, const slayer3d_input_manager *input,
                                                 Uint32 tick, void *buffer, size_t buffer_size, size_t *out_size,
                                                 char *error_buffer, int error_buffer_size);

    /**
     * @brief Encode a client-to-host input packet by runtime binding semantic.
     */
    bool slayer3d_game_data_encode_network_runtime_input(const slayer3d_game_data_runtime *runtime,
                                                         const char *binding_name, const slayer3d_input_manager *input,
                                                         Uint32 tick, void *buffer, size_t buffer_size,
                                                         size_t *out_size, char *error_buffer, int error_buffer_size);

    /**
     * @brief Encode and send a client-to-host input packet by runtime binding.
     */
    bool slayer3d_game_data_send_network_runtime_input(const slayer3d_game_data_runtime *runtime,
                                                       slayer3d_network_session *session, const char *binding_name,
                                                       const slayer3d_input_manager *input, Uint32 tick,
                                                       char *error_buffer, int error_buffer_size);

    /**
     * @brief Decode and apply an authored client-to-host input packet.
     *
     * The packet must match the runtime schema hash and reference a
     * client-to-host input channel in the loaded `network` schema. Decoded
     * action values are applied to @p input as action overrides. On failure,
     * no overrides are changed.
     *
     * @param runtime Runtime containing the authored network schema and actions.
     * @param input Input manager that should receive replicated action overrides.
     * @param packet Source packet buffer.
     * @param packet_size Source packet size in bytes.
     * @param out_tick Receives the client input tick, if non-NULL.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the packet was decoded and all action overrides were applied.
     */
    bool slayer3d_game_data_apply_network_input(const slayer3d_game_data_runtime *runtime,
                                                slayer3d_input_manager *input, const void *packet, size_t packet_size,
                                                Uint32 *out_tick, char *error_buffer, int error_buffer_size);

    /**
     * @brief Decode and apply a client-to-host input packet expected by runtime binding.
     *
     * The packet must be a valid input packet for the concrete replication
     * channel mapped by @p binding_name. On failure, no input overrides are
     * changed.
     */
    bool slayer3d_game_data_apply_network_runtime_input(const slayer3d_game_data_runtime *runtime,
                                                        const char *binding_name, slayer3d_input_manager *input,
                                                        const void *packet, size_t packet_size, Uint32 *out_tick,
                                                        char *error_buffer, int error_buffer_size);

    /**
     * @brief Clear action overrides declared by an authored input replication channel.
     *
     * This is useful when a peer disconnects or a networked scene exits so
     * remote action overrides cannot leak into local play.
     *
     * @param runtime Runtime containing the authored network schema and actions.
     * @param replication_name Authored client-to-host replication channel name.
     * @param input Input manager whose replicated overrides should be cleared.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when all authored input overrides were cleared.
     */
    bool slayer3d_game_data_clear_network_input_overrides(const slayer3d_game_data_runtime *runtime,
                                                          const char *replication_name, slayer3d_input_manager *input,
                                                          char *error_buffer, int error_buffer_size);

    /**
     * @brief Encode an authored network control message packet.
     *
     * The named control message must exist in the loaded `network`
     * `control_messages` array. The packet includes a deterministic header,
     * schema hash, control-message index, and tick. Callers provide the
     * destination buffer; no allocation is performed.
     *
     * @param runtime Runtime containing the authored network schema.
     * @param control_name Authored control message name.
     * @param tick Simulation or wall-clock tick to include in the packet.
     * @param buffer Destination packet buffer.
     * @param buffer_size Destination buffer size in bytes.
     * @param out_size Receives written packet size in bytes.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the control packet was encoded.
     */
    bool slayer3d_game_data_encode_network_control(const slayer3d_game_data_runtime *runtime, const char *control_name,
                                                   Uint32 tick, void *buffer, size_t buffer_size, size_t *out_size,
                                                   char *error_buffer, int error_buffer_size);

    /**
     * @brief Encode an authored control packet by runtime binding semantic.
     *
     * @p binding_name is resolved through `network.runtime_bindings.controls`
     * before encoding. Use this from generic session/runtime code so the loop
     * does not need to know concrete control-message names.
     */
    bool slayer3d_game_data_encode_network_runtime_control(const slayer3d_game_data_runtime *runtime,
                                                           const char *binding_name, Uint32 tick, void *buffer,
                                                           size_t buffer_size, size_t *out_size, char *error_buffer,
                                                           int error_buffer_size);

    /**
     * @brief Decode an authored network control message packet.
     *
     * The packet must match the runtime schema hash and reference an authored
     * control message in the loaded `network` schema. On success @p out_control
     * receives runtime-owned descriptor strings and the packet tick.
     *
     * @param runtime Runtime containing the authored network schema.
     * @param packet Source packet buffer.
     * @param packet_size Source packet size in bytes.
     * @param out_control Receives decoded control metadata.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the packet was decoded completely.
     */
    bool slayer3d_game_data_decode_network_control(const slayer3d_game_data_runtime *runtime, const void *packet,
                                                   size_t packet_size, slayer3d_game_data_network_control *out_control,
                                                   char *error_buffer, int error_buffer_size);

    /**
     * @brief Decode an authored control packet and resolve its runtime binding.
     *
     * On success, @p out_binding receives the semantic name from
     * `network.runtime_bindings.controls`, while @p out_control receives the
     * concrete decoded control metadata. Both strings are owned by @p runtime.
     */
    bool slayer3d_game_data_decode_network_runtime_control(const slayer3d_game_data_runtime *runtime,
                                                           const void *packet, size_t packet_size,
                                                           const char **out_binding,
                                                           slayer3d_game_data_network_control *out_control,
                                                           char *error_buffer, int error_buffer_size);

    /**
     * @brief Encode and send an authored control packet by runtime binding.
     *
     * This helper is transport-light: the caller still owns the session and
     * higher-level flow, but packet naming, encoding, and send error reporting
     * are centralized in the engine.
     */
    bool slayer3d_game_data_send_network_runtime_control(const slayer3d_game_data_runtime *runtime,
                                                         slayer3d_network_session *session, const char *binding_name,
                                                         Uint32 tick, char *error_buffer, int error_buffer_size);

    /**
     * @brief Decode and emit an authored network control message signal.
     *
     * This validates the packet with slayer3d_game_data_decode_network_control()
     * and emits the control message's authored signal on the runtime session
     * bus. The signal payload includes `network_control`, `network_direction`,
     * and `network_tick` fields.
     *
     * @param runtime Runtime containing the authored network schema and session bus.
     * @param packet Source packet buffer.
     * @param packet_size Source packet size in bytes.
     * @param out_control Receives decoded control metadata, if non-NULL.
     * @param error_buffer Optional buffer for the first failure reason.
     * @param error_buffer_size Size of @p error_buffer in bytes.
     * @return true when the packet was decoded and the signal emitted.
     */
    bool slayer3d_game_data_apply_network_control(slayer3d_game_data_runtime *runtime, const void *packet,
                                                  size_t packet_size, slayer3d_game_data_network_control *out_control,
                                                  char *error_buffer, int error_buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* SLAYER3D_GAME_DATA_NETWORK_RUNTIME_H */

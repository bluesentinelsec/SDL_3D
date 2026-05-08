local pacman = {}

local MAP = "map.maze"
local PACMAN_START = { col = 1, row = 1, dx = 1, dy = 0 }
local POWER_SECONDS = 8.0
local GHOST_IDLE_SECONDS = 3.6
local PACMAN_Z = 0.28
local POWER_Z = 0.42

local GHOSTS = {
    "entity.ghost.red",
    "entity.ghost.pink",
    "entity.ghost.cyan",
    "entity.ghost.orange",
}

local function clamp(value, lo, hi)
    if value < lo then
        return lo
    end
    if value > hi then
        return hi
    end
    return value
end

local function game_actor(ctx)
    return ctx:actor("entity.game")
end

local function set_cell_actor(ctx, actor, col, row, z)
    if actor == nil then
        return
    end

    local position = ctx:grid_cell_to_world(MAP, col, row)
    if position ~= nil then
        actor.position = Vec3(position.x, position.y, z or actor.position.z)
    end
    actor:set_int("grid_col", col)
    actor:set_int("grid_row", row)
    actor:set_int("grid_from_col", col)
    actor:set_int("grid_from_row", row)
    actor:set_int("grid_target_col", -1)
    actor:set_int("grid_target_row", -1)
    actor:set_float("grid_progress", 0.0)
end

local function set_grid_direction(actor, dx, dy)
    if actor == nil then
        return
    end

    actor:set_int("grid_dir_x", dx)
    actor:set_int("grid_dir_y", dy)
    actor:set_int("grid_next_dir_x", dx)
    actor:set_int("grid_next_dir_y", dy)
end

local function reset_pacman(ctx)
    local actor = ctx:actor("entity.pacman")
    set_cell_actor(ctx, actor, PACMAN_START.col, PACMAN_START.row, PACMAN_Z)
    set_grid_direction(actor, PACMAN_START.dx, PACMAN_START.dy)
    if actor ~= nil then
        actor:set_float("grid_speed", 6.8)
        actor:set_float("mouth_phase", 0.0)
    end
end

local function reset_ghost(ctx, name, idle_seconds)
    local ghost = ctx:actor(name)
    if ghost == nil then
        return
    end

    local col = ghost:get_int("home_col", ghost:get_int("grid_col", 10))
    local row = ghost:get_int("home_row", ghost:get_int("grid_row", 8))
    set_cell_actor(ctx, ghost, col, row, PACMAN_Z)
    set_grid_direction(ghost, 0, 0)
    ghost:set_float("idle_timer", idle_seconds or 0.0)
    ghost:set_float("phase", ghost:get_float("phase", 0.0))
end

local function spawn_effect(ctx, pool, position, ttl)
    if position == nil then
        return nil
    end

    return ctx:spawn(pool, {
        position = Vec3(position.x, position.y, position.z or 0.36),
        properties = {
            age = 0.0,
            ttl = ttl or 0.35
        }
    })
end

local function reset_runtime_actors(ctx)
    ctx:despawn_by_tag("maze_geometry", "new game")
    ctx:despawn_by_tag("collectible", "new game")
    ctx:despawn_by_tag("effect", "new game")
end

function pacman.start(game, _, ctx)
    if game == nil then
        game = game_actor(ctx)
    end
    if game == nil then
        return false
    end

    reset_runtime_actors(ctx)
    reset_pacman(ctx)
    for index, name in ipairs(GHOSTS) do
        reset_ghost(ctx, name, (index - 1) * 0.6)
    end

    game:set_int("score", 0)
    game:set_int("pellets_remaining", 0)
    game:set_int("defeat_warnings", 0)
    game:set_int("ghosts_defeated", 0)
    game:set_float("power_timer", 0.0)
    game:set_float("warning_timer", 0.0)
    game:set_float("effect_timer", 0.0)
    game:set_bool("power_active", false)
    game:set_bool("won", false)
    game:set_bool("game_over", false)
    return true
end

function pacman.after_spawn(game, _, ctx)
    if game == nil then
        game = game_actor(ctx)
    end
    if game == nil then
        return false
    end

    local spawned = tonumber(ctx:state_get("pacman_spawned_collectibles", "0")) or 0
    game:set_int("pellets_remaining", spawned)
    return true
end

local function update_power_state(ctx, game, dt)
    local pac = ctx:actor("entity.pacman")
    local timer = math.max(game:get_float("power_timer", 0.0) - dt, 0.0)
    game:set_float("power_timer", timer)
    game:set_bool("power_active", timer > 0.0)

    if pac ~= nil then
        local position = pac.position
        local phase = pac:get_float("mouth_phase", 0.0) + dt * 8.0
        pac:set_float("mouth_phase", phase)
        local lift = timer > 0.0 and (POWER_Z + math.sin(phase * 2.1) * 0.035) or PACMAN_Z
        pac.position = Vec3(position.x, position.y, lift)
        pac:set_float("grid_speed", timer > 0.0 and 7.35 or 6.8)

        if timer > 0.0 then
            local effect_timer = game:get_float("effect_timer", 0.0) - dt
            if effect_timer <= 0.0 then
                spawn_effect(ctx, "pool.power_bursts", Vec3(position.x, position.y, POWER_Z), 0.42)
                effect_timer = 0.20
            end
            game:set_float("effect_timer", effect_timer)
        else
            game:set_float("effect_timer", 0.0)
        end
    end
end

local function collect_pellets(ctx, game)
    local player = ctx:actor("entity.pacman")
    if player == nil or game:get_bool("won", false) then
        return
    end

    local player_col = player:get_int("grid_col", -1)
    local player_row = player:get_int("grid_row", -1)
    local remaining = game:get_int("pellets_remaining", 0)

    local function collect_actor(pellet)
        if pellet == nil then
            return
        end
        local kind = pellet:get_string("kind", "pellet")
        local points = pellet:get_int("points", 10)
        local position = pellet.position
        local effect_pool = kind == "power" and "pool.power_bursts" or "pool.pellet_bursts"
        local ttl = kind == "power" and 0.72 or 0.34

        game:set_int("score", game:get_int("score", 0) + points)
        remaining = math.max(remaining - 1, 0)
        spawn_effect(ctx, effect_pool, Vec3(position.x, position.y, 0.36), ttl)
        if kind == "power" then
            game:set_float("power_timer", POWER_SECONDS)
            game:set_bool("power_active", true)
        end
        pellet:despawn("collected")
    end

    collect_actor(ctx:grid_actor_at("map.maze", "pool.pellets", player_col, player_row))
    collect_actor(ctx:grid_actor_at("map.maze", "pool.power_pellets", player_col, player_row))

    game:set_int("pellets_remaining", remaining)
    if remaining <= 0 then
        game:set_bool("won", true)
    end
end

local function target_for_ghost(ghost, player, frightened)
    if frightened then
        return {
            col = ghost:get_int("scatter_col", ghost:get_int("grid_col", 1)),
            row = ghost:get_int("scatter_row", ghost:get_int("grid_row", 1))
        }
    end

    local player_col = player:get_int("grid_col", 1)
    local player_row = player:get_int("grid_row", 1)
    local name = ghost.name or ""

    if name == "entity.ghost.pink" then
        return {
            col = player_col + player:get_int("grid_dir_x", 0) * 3,
            row = player_row + player:get_int("grid_dir_y", 0) * 3
        }
    end

    if name == "entity.ghost.cyan" then
        local phase = ghost:get_float("phase", 0.0)
        return {
            col = player_col + math.floor(math.sin(phase * 7.0) * 4.0),
            row = player_row + math.floor(math.cos(phase * 5.0) * 3.0)
        }
    end

    if name == "entity.ghost.orange" then
        local dx = math.abs(ghost:get_int("grid_col", 1) - player_col)
        local dy = math.abs(ghost:get_int("grid_row", 1) - player_row)
        if dx + dy < 6 then
            return {
                col = ghost:get_int("scatter_col", 1),
                row = ghost:get_int("scatter_row", 15)
            }
        end
    end

    return { col = player_col, row = player_row }
end

local function choose_fallback_direction(ctx, ghost)
    local col = ghost:get_int("grid_col", 1)
    local row = ghost:get_int("grid_row", 1)
    local current_dx = ghost:get_int("grid_dir_x", 0)
    local current_dy = ghost:get_int("grid_dir_y", 0)
    local phase = ghost:get_float("phase", 0.0)
    local directions = {
        { dx = current_dx, dy = current_dy },
        { dx = 1, dy = 0 },
        { dx = 0, dy = -1 },
        { dx = -1, dy = 0 },
        { dx = 0, dy = 1 },
    }
    local offset = math.floor(phase * 11.0) % #directions

    for i = 1, #directions do
        local direction = directions[((i + offset - 1) % #directions) + 1]
        if (direction.dx ~= 0 or direction.dy ~= 0) and ctx:grid_walkable(MAP, col + direction.dx, row + direction.dy) then
            return direction.dx, direction.dy
        end
    end

    return 0, 0
end

local function update_ghosts(ctx, game, dt)
    local player = ctx:actor("entity.pacman")
    if player == nil then
        return
    end

    local frightened = game:get_float("power_timer", 0.0) > 0.0
    for index, name in ipairs(GHOSTS) do
        local ghost = ctx:actor(name)
        if ghost ~= nil then
            local phase = ghost:get_float("phase", 0.0) + dt
            ghost:set_float("phase", phase)

            local idle = math.max(ghost:get_float("idle_timer", 0.0) - dt, 0.0)
            ghost:set_float("idle_timer", idle)
            if idle > 0.0 then
                set_cell_actor(ctx, ghost, ghost:get_int("home_col", 10), ghost:get_int("home_row", 8),
                               PACMAN_Z + (math.sin(phase * 15.0) > 0.0 and 0.12 or 0.0))
                set_grid_direction(ghost, 0, 0)
            elseif ghost:get_int("grid_target_col", -1) < 0 then
                local target = target_for_ghost(ghost, player, frightened)
                local step = ctx:grid_next_step(MAP, ghost:get_int("grid_col", 1), ghost:get_int("grid_row", 1),
                                                target.col, target.row)
                local dx = 0
                local dy = 0
                if step ~= nil then
                    dx = clamp(step.col - ghost:get_int("grid_col", 1), -1, 1)
                    dy = clamp(step.row - ghost:get_int("grid_row", 1), -1, 1)
                end
                if dx == 0 and dy == 0 then
                    dx, dy = choose_fallback_direction(ctx, ghost)
                end
                set_grid_direction(ghost, dx, dy)
                ghost:set_float("grid_speed", frightened and 3.15 or (4.0 + index * 0.12))
            end
        end
    end
end

local function handle_ghost_contacts(ctx, game, dt)
    local player = ctx:actor("entity.pacman")
    if player == nil then
        return
    end

    local warning_timer = math.max(game:get_float("warning_timer", 0.0) - dt, 0.0)
    game:set_float("warning_timer", warning_timer)

    local player_col = player:get_int("grid_col", -1)
    local player_row = player:get_int("grid_row", -1)
    local powered = game:get_float("power_timer", 0.0) > 0.0

    for _, name in ipairs(GHOSTS) do
        local ghost = ctx:actor(name)
        if ghost ~= nil and ghost:get_float("idle_timer", 0.0) <= 0.0 and ghost:get_int("grid_col", -2) == player_col and
            ghost:get_int("grid_row", -2) == player_row then
            if powered then
                local position = ghost.position
                game:set_int("score", game:get_int("score", 0) + 200)
                game:set_int("ghosts_defeated", game:get_int("ghosts_defeated", 0) + 1)
                spawn_effect(ctx, "pool.ghost_bursts", Vec3(position.x, position.y, 0.40), 0.85)
                reset_ghost(ctx, name, GHOST_IDLE_SECONDS)
            elseif warning_timer <= 0.0 then
                local position = player.position
                game:set_int("defeat_warnings", game:get_int("defeat_warnings", 0) + 1)
                game:set_float("warning_timer", 1.1)
                spawn_effect(ctx, "pool.ghost_bursts", Vec3(position.x, position.y, 0.40), 0.45)
            end
        end
    end
end

local function update_effects(ctx, dt)
    for _, effect in ipairs(ctx:active_actors_with_tags("effect")) do
        local age = effect:get_float("age", 0.0) + dt
        effect:set_float("age", age)
        if age >= effect:get_float("ttl", 0.4) then
            effect:despawn("effect expired")
        else
            local position = effect.position
            effect.position = Vec3(position.x, position.y, position.z + dt * 0.10)
        end
    end
end

function pacman.update(_, _, ctx)
    local game = game_actor(ctx)
    if game == nil or game:get_bool("game_over", false) then
        return true
    end

    local dt = ctx.dt
    update_power_state(ctx, game, dt)
    collect_pellets(ctx, game)
    update_ghosts(ctx, game, dt)
    handle_ghost_contacts(ctx, game, dt)
    update_effects(ctx, dt)
    return true
end

return pacman

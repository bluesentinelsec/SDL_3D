local space_invaders = {}

local FIELD_LEFT = -7.65
local FIELD_RIGHT = 7.65
local PLAYER_Y = -4.08
local PLAYER_Z = 0.12
local SHOT_TOP = 4.85
local SHOT_BOTTOM = -4.75
local ALIEN_ROWS = 5
local ALIEN_COLS = 11

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

local function set_game_over(game, won)
    game:set_bool("game_over", true)
    game:set_bool("won", won and true or false)
end

local function despawn_runtime_actors(ctx)
    ctx:despawn_by_tag("alien", "new wave")
    ctx:despawn_by_tag("projectile", "new wave")
    ctx:despawn_by_tag("shield", "new wave")
    ctx:despawn_by_tag("explosion", "new wave")
end

local function spawn_explosion(ctx, position, ttl)
    if position == nil then
        return
    end

    ctx:spawn("pool.explosions", {
        position = Vec3(position.x, position.y, 0.34),
        properties = {
            age = 0.0,
            ttl = ttl or 0.42
        }
    })
end

local function spawn_shields(ctx)
    local starts = { -5.85, -2.0, 2.0, 5.85 }
    local pattern = {
        { true, true, true, true, true, true },
        { true, true, true, true, true, true },
        { true, true, false, false, true, true },
        { true, false, false, false, false, true },
    }

    for _, base_x in ipairs(starts) do
        for row = 1, #pattern do
            for col = 1, #pattern[row] do
                if pattern[row][col] then
                    ctx:spawn("pool.shields", {
                        position = Vec3(base_x + (col - 3.5) * 0.34, -2.65 - (row - 1) * 0.26, 0.08),
                        properties = {
                            damage = 0
                        }
                    })
                end
            end
        end
    end
end

local function spawn_alien_grid(ctx, game)
    local wave = game:get_int("wave", 1)
    local remaining = 0

    game:set_float("formation_x", 0.0)
    game:set_float("formation_y", 0.0)
    game:set_int("formation_direction", 1)
    game:set_float("alien_fire_timer", 0.7)

    for row = 0, ALIEN_ROWS - 1 do
        for col = 0, ALIEN_COLS - 1 do
            local points = 10 + (ALIEN_ROWS - row - 1) * 5
            local base_x = -5.0 + col * 1.0
            local base_y = 3.35 - row * 0.58
            local alien = ctx:spawn("pool.aliens", {
                position = Vec3(base_x, base_y, 0.14),
                properties = {
                    row = row,
                    col = col,
                    base_x = base_x,
                    base_y = base_y,
                    points = points,
                    phase = (row * 0.37) + (col * 0.19) + wave * 0.11
                }
            })
            if alien ~= nil then
                remaining = remaining + 1
            end
        end
    end

    game:set_int("aliens_remaining", remaining)
end

local function reset_players(ctx)
    local p1 = ctx:actor("entity.player")
    local p2 = ctx:actor("entity.player2")
    if p1 ~= nil then
        p1.position = Vec3(-0.55, PLAYER_Y, PLAYER_Z)
        p1:set_float("fire_timer", 0.0)
    end
    if p2 ~= nil then
        p2.position = Vec3(0.55, PLAYER_Y, PLAYER_Z)
        p2:set_float("fire_timer", 0.0)
    end
end

function space_invaders.start(game, _, ctx)
    if game == nil then
        game = game_actor(ctx)
    end
    if game == nil then
        return false
    end

    despawn_runtime_actors(ctx)
    reset_players(ctx)

    game:set_int("score", 0)
    game:set_int("lives", ctx:state_get("match_mode", "single") == "local" and 5 or 3)
    game:set_int("wave", 1)
    game:set_bool("game_over", false)
    game:set_bool("won", false)
    game:set_bool("paused", false)

    spawn_shields(ctx)
    spawn_alien_grid(ctx, game)
    return true
end

local function fire_from_player(player, ctx, owner)
    local game = game_actor(ctx)
    if player == nil or game == nil or not player:is_active() or game:get_bool("game_over", false) then
        return true
    end

    local timer = player:get_float("fire_timer", 0.0)
    if timer > 0.0 then
        return true
    end

    local position = player.position
    local shot = ctx:spawn("pool.player_shots", {
        position = Vec3(position.x, position.y + 0.42, 0.30),
        properties = {
            owner = owner,
            velocity = Vec3(0.0, 11.5, 0.0)
        }
    })

    if shot ~= nil then
        player:set_float("fire_timer", player:get_float("fire_cooldown", 0.22))
    end
    return true
end

function space_invaders.fire_player_one(player, _, ctx)
    return fire_from_player(player or ctx:actor("entity.player"), ctx, 1)
end

function space_invaders.fire_player_two(player, _, ctx)
    return fire_from_player(player or ctx:actor("entity.player2"), ctx, 2)
end

local function update_player_cooldowns(ctx, dt)
    for _, name in ipairs({ "entity.player", "entity.player2" }) do
        local player = ctx:actor(name)
        if player ~= nil and player:is_active() then
            player:set_float("fire_timer", math.max(player:get_float("fire_timer", 0.0) - dt, 0.0))
        end
    end
end

local function update_projectiles(ctx, tag, dt)
    for _, shot in ipairs(ctx:active_actors_with_tags(tag)) do
        local position = shot.position
        local velocity = shot.velocity or Vec3(0.0, 0.0, 0.0)
        local next_position = Vec3(position.x + velocity.x * dt, position.y + velocity.y * dt, position.z)
        shot.position = next_position
        if next_position.y > SHOT_TOP or next_position.y < SHOT_BOTTOM then
            shot:despawn("out of bounds")
        end
    end
end

local function point_hits_actor(point, radius, actor, extra)
    if actor == nil or not actor:is_active() then
        return false
    end
    local p = actor.position
    local half_width = actor:get_float("half_width", 0.25) + radius + (extra or 0.0)
    local half_height = actor:get_float("half_height", 0.25) + radius + (extra or 0.0)
    return math.abs(point.x - p.x) <= half_width and math.abs(point.y - p.y) <= half_height
end

local function damage_shield(ctx, shield, shot)
    local damage = shield:get_int("damage", 0) + 1
    spawn_explosion(ctx, shot.position, 0.22)
    shot:despawn("shield impact")
    if damage >= 3 then
        shield:despawn("shield destroyed")
    else
        shield:set_int("damage", damage)
        local position = shield.position
        shield.position = Vec3(position.x, position.y, position.z + 0.015 * damage)
    end
end

local function handle_player_projectile_hits(ctx, game)
    local remaining = game:get_int("aliens_remaining", 0)
    for _, shot in ipairs(ctx:active_actors_with_tags("player_projectile")) do
        local shot_position = shot.position
        local radius = shot:get_float("radius", 0.1)
        local hit = false

        for _, alien in ipairs(ctx:active_actors_with_tags("alien")) do
            if point_hits_actor(shot_position, radius, alien, 0.02) then
                spawn_explosion(ctx, alien.position, 0.45)
                game:set_int("score", game:get_int("score", 0) + alien:get_int("points", 10))
                alien:despawn("shot hit")
                shot:despawn("alien hit")
                remaining = math.max(remaining - 1, 0)
                hit = true
                break
            end
        end

        if not hit then
            for _, shield in ipairs(ctx:active_actors_with_tags("shield")) do
                if point_hits_actor(shot_position, radius, shield, 0.0) then
                    damage_shield(ctx, shield, shot)
                    break
                end
            end
        end
    end
    game:set_int("aliens_remaining", remaining)
end

local function handle_alien_projectile_hits(ctx, game)
    for _, shot in ipairs(ctx:active_actors_with_tags("alien_projectile")) do
        local shot_position = shot.position
        local radius = shot:get_float("radius", 0.11)
        local consumed = false

        for _, shield in ipairs(ctx:active_actors_with_tags("shield")) do
            if point_hits_actor(shot_position, radius, shield, 0.0) then
                damage_shield(ctx, shield, shot)
                consumed = true
                break
            end
        end

        if not consumed then
            for _, player in ipairs({ ctx:actor("entity.player"), ctx:actor("entity.player2") }) do
                if point_hits_actor(shot_position, radius, player, 0.05) then
                    local lives = math.max(game:get_int("lives", 1) - 1, 0)
                    game:set_int("lives", lives)
                    spawn_explosion(ctx, player.position, 0.60)
                    shot:despawn("player hit")
                    if lives <= 0 then
                        set_game_over(game, false)
                    end
                    break
                end
            end
        end
    end
end

local function update_alien_grid(ctx, game, dt)
    local aliens = ctx:active_actors_with_tags("alien")
    local remaining = #aliens
    game:set_int("aliens_remaining", remaining)

    if remaining <= 0 then
        local next_wave = game:get_int("wave", 1) + 1
        if next_wave > 3 then
            set_game_over(game, true)
            return
        end
        game:set_int("wave", next_wave)
        spawn_alien_grid(ctx, game)
        return
    end

    local wave = game:get_int("wave", 1)
    local direction = game:get_int("formation_direction", 1)
    local formation_x = game:get_float("formation_x", 0.0)
    local formation_y = game:get_float("formation_y", 0.0)
    local speed = 0.52 + wave * 0.10 + (ALIEN_ROWS * ALIEN_COLS - remaining) * 0.012

    formation_x = formation_x + direction * speed * dt

    local min_x = 100.0
    local max_x = -100.0
    local lowest_y = 100.0
    for _, alien in ipairs(aliens) do
        local base_x = alien:get_float("base_x", 0.0) + formation_x
        local base_y = alien:get_float("base_y", 0.0) + formation_y
        min_x = math.min(min_x, base_x - alien:get_float("half_width", 0.31))
        max_x = math.max(max_x, base_x + alien:get_float("half_width", 0.31))
        lowest_y = math.min(lowest_y, base_y)
    end

    if max_x > FIELD_RIGHT or min_x < FIELD_LEFT then
        direction = -direction
        formation_x = clamp(formation_x, -0.85, 0.85)
        formation_y = formation_y - (0.23 + wave * 0.035)
        game:set_int("formation_direction", direction)
    end

    game:set_float("formation_x", formation_x)
    game:set_float("formation_y", formation_y)

    local time = ctx:state_get("invader_time", 0.0) + dt
    ctx:state_set("invader_time", time)
    for _, alien in ipairs(aliens) do
        local phase = alien:get_float("phase", 0.0)
        local base_x = alien:get_float("base_x", 0.0) + formation_x
        local base_y = alien:get_float("base_y", 0.0) + formation_y
        alien.position = Vec3(base_x, base_y + math.sin(time * 4.0 + phase) * 0.035, 0.14)
    end

    if lowest_y + formation_y < -3.35 then
        set_game_over(game, false)
    end
end

local function spawn_alien_shot(ctx, alien, wave)
    if alien == nil then
        return
    end
    local position = alien.position
    ctx:spawn("pool.alien_shots", {
        position = Vec3(position.x, position.y - 0.32, 0.28),
        properties = {
            velocity = Vec3((ctx:random() - 0.5) * (0.45 + wave * 0.10), -(4.4 + wave * 0.55), 0.0)
        }
    })
end

local function update_alien_fire(ctx, game, dt)
    local timer = game:get_float("alien_fire_timer", 0.0) - dt
    if timer > 0.0 then
        game:set_float("alien_fire_timer", timer)
        return
    end

    local aliens = ctx:active_actors_with_tags("alien")
    if #aliens > 0 then
        local index = math.floor(ctx:random() * #aliens) + 1
        spawn_alien_shot(ctx, aliens[index], game:get_int("wave", 1))
    end

    local wave = game:get_int("wave", 1)
    local remaining_factor = math.max(#aliens / (ALIEN_ROWS * ALIEN_COLS), 0.18)
    game:set_float("alien_fire_timer", 0.40 + ctx:random() * (1.0 * remaining_factor) - wave * 0.035)
end

local function update_explosions(ctx, dt)
    for _, explosion in ipairs(ctx:active_actors_with_tags("explosion")) do
        local age = explosion:get_float("age", 0.0) + dt
        explosion:set_float("age", age)
        if age >= explosion:get_float("ttl", 0.42) then
            explosion:despawn("effect expired")
        end
    end
end

function space_invaders.update(_, _, ctx)
    local game = game_actor(ctx)
    if game == nil then
        return false
    end

    local dt = ctx.dt
    update_player_cooldowns(ctx, dt)
    update_explosions(ctx, dt)

    if game:get_bool("game_over", false) then
        return true
    end

    update_projectiles(ctx, "player_projectile", dt)
    update_projectiles(ctx, "alien_projectile", dt)
    handle_player_projectile_hits(ctx, game)
    handle_alien_projectile_hits(ctx, game)
    update_alien_grid(ctx, game, dt)
    update_alien_fire(ctx, game, dt)
    return true
end

return space_invaders

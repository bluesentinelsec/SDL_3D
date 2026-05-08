local armalite = {}

local FIELD_LEFT = -8.65
local FIELD_RIGHT = 8.85
local FIELD_TOP = 4.35
local FIELD_BOTTOM = -4.30
local PLAYER_X = -6.55
local PLAYER_Z = 0.18

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

local function despawn_runtime_actors(ctx)
    ctx:despawn_by_tag("enemy", "new sortie")
    ctx:despawn_by_tag("asteroid", "new sortie")
    ctx:despawn_by_tag("projectile", "new sortie")
    ctx:despawn_by_tag("explosion", "new sortie")
end

local function spawn_explosion(ctx, position, ttl)
    if position == nil then
        return
    end

    ctx:spawn("pool.explosions", {
        position = Vec3(position.x, position.y, 0.42),
        properties = {
            age = 0.0,
            ttl = ttl or 0.36
        }
    })
end

local function reset_player(player, x, y)
    if player == nil then
        return
    end
    player.position = Vec3(x, y, PLAYER_Z)
    player:set_float("fire_timer", 0.0)
end

local function set_game_over(game, won)
    game:set_bool("game_over", true)
    game:set_bool("won", won and true or false)
end

function armalite.start(game, _, ctx)
    if game == nil then
        game = game_actor(ctx)
    end
    if game == nil then
        return false
    end

    despawn_runtime_actors(ctx)
    reset_player(ctx:actor("entity.player"), PLAYER_X, 0.0)
    reset_player(ctx:actor("entity.player2"), PLAYER_X + 0.55, -0.72)

    game:set_int("score", 0)
    game:set_int("lives", ctx:state_get("match_mode", "single") == "local" and 5 or 3)
    game:set_int("wave", 1)
    game:set_float("distance", 0.0)
    game:set_float("enemy_spawn_timer", 0.18)
    game:set_float("asteroid_spawn_timer", 1.1)
    game:set_float("enemy_fire_timer", 0.8)
    game:set_int("threats_active", 0)
    game:set_bool("game_over", false)
    game:set_bool("won", false)
    game:set_bool("paused", false)
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
        position = Vec3(position.x + 0.62, position.y, 0.34),
        properties = {
            owner = owner,
            velocity = Vec3(12.0 + owner * 0.45, 0.0, 0.0),
            damage = owner == 1 and 1 or 2
        }
    })
    if shot ~= nil then
        player:set_float("fire_timer", player:get_float("fire_cooldown", 0.15))
    end
    return true
end

function armalite.fire_player_one(player, _, ctx)
    return fire_from_player(player, ctx, 1)
end

function armalite.fire_player_two(player, _, ctx)
    return fire_from_player(player, ctx, 2)
end

local function random_range(ctx, lo, hi)
    return lo + (hi - lo) * ctx:random()
end

local function spawn_enemy(ctx, game)
    local wave = game:get_int("wave", 1)
    local y = random_range(ctx, FIELD_BOTTOM + 0.65, FIELD_TOP - 0.65)
    local cruiser = wave >= 2 and ctx:random() > 0.72
    local pool = cruiser and "pool.cruisers" or "pool.drones"
    local speed = cruiser and -(1.55 + wave * 0.08) or -(2.65 + wave * 0.12)
    local enemy = ctx:spawn(pool, {
        position = Vec3(FIELD_RIGHT + 0.35, y, cruiser and 0.24 or 0.21),
        properties = {
            base_y = y,
            phase = random_range(ctx, 0.0, 6.28),
            amplitude = cruiser and random_range(ctx, 0.15, 0.38) or random_range(ctx, 0.24, 0.72),
            velocity = Vec3(speed, 0.0, 0.0),
            hp = cruiser and (3 + math.floor(wave / 3)) or 1
        }
    })
    return enemy ~= nil
end

local function spawn_asteroid(ctx, game)
    local wave = game:get_int("wave", 1)
    local radius = random_range(ctx, 0.28, 0.58)
    ctx:spawn("pool.asteroids", {
        position = Vec3(FIELD_RIGHT + 0.45, random_range(ctx, FIELD_BOTTOM + 0.55, FIELD_TOP - 0.55), 0.13),
        properties = {
            radius = radius,
            half_width = radius,
            half_height = radius,
            hp = math.max(2, math.floor(radius * 7.0)),
            velocity = Vec3(-(1.65 + wave * 0.08 + ctx:random() * 0.55), 0.0, 0.0)
        }
    })
end

local function move_actor(actor, dt)
    local velocity = actor.velocity
    local position = actor.position
    actor.position = Vec3(position.x + velocity.x * dt, position.y + velocity.y * dt, position.z)
end

local function update_movers(ctx, tag, dt)
    for _, actor in ipairs(ctx:active_actors_with_tags(tag)) do
        move_actor(actor, dt)
        local position = actor.position
        if position.x < FIELD_LEFT - 1.1 or position.x > FIELD_RIGHT + 1.4 or position.y < FIELD_BOTTOM - 1.0 or position.y > FIELD_TOP + 1.0 then
            actor:despawn("out of bounds")
        end
    end
end

local function aabb_overlap(a, b)
    local ap = a.position
    local bp = b.position
    local aw = a:get_float("half_width", a:get_float("radius", 0.12))
    local ah = a:get_float("half_height", a:get_float("radius", 0.12))
    local bw = b:get_float("half_width", b:get_float("radius", 0.12))
    local bh = b:get_float("half_height", b:get_float("radius", 0.12))
    return math.abs(ap.x - bp.x) <= aw + bw and math.abs(ap.y - bp.y) <= ah + bh
end

local function hit_threat(ctx, game, shot, threat)
    local damage = shot:get_int("damage", 1)
    local hp = threat:get_int("hp", 1) - damage
    shot:despawn("impact")
    if hp <= 0 then
        game:set_int("score", game:get_int("score", 0) + threat:get_int("points", 100))
        spawn_explosion(ctx, threat.position, 0.42)
        threat:despawn("destroyed")
    else
        threat:set_int("hp", hp)
        spawn_explosion(ctx, shot.position, 0.18)
    end
end

local function update_player_shot_collisions(ctx, game)
    for _, shot in ipairs(ctx:active_actors_with_tags("player_projectile")) do
        local consumed = false
        for _, enemy in ipairs(ctx:active_actors_with_tags("enemy")) do
            if not consumed and aabb_overlap(shot, enemy) then
                hit_threat(ctx, game, shot, enemy)
                consumed = true
            end
        end
        for _, asteroid in ipairs(ctx:active_actors_with_tags("asteroid")) do
            if not consumed and aabb_overlap(shot, asteroid) then
                hit_threat(ctx, game, shot, asteroid)
                consumed = true
            end
        end
    end
end

local function damage_player(ctx, game, player, reason)
    if player == nil or not player:is_active() or game:get_bool("game_over", false) then
        return
    end

    spawn_explosion(ctx, player.position, 0.56)
    local lives = game:get_int("lives", 1) - 1
    game:set_int("lives", lives)
    if lives <= 0 then
        set_game_over(game, false)
        return
    end

    local offset = player:get_int("player_id", 1) == 1 and 0.0 or -0.72
    player.position = Vec3(PLAYER_X + (player:get_int("player_id", 1) - 1) * 0.55, offset, PLAYER_Z)
    player:set_float("fire_timer", 0.35)
end

local function update_player_damage(ctx, game)
    local players = ctx:active_actors_with_tags("player")
    for _, shot in ipairs(ctx:active_actors_with_tags("enemy_projectile")) do
        for _, player in ipairs(players) do
            if shot:is_active() and aabb_overlap(shot, player) then
                shot:despawn("player hit")
                damage_player(ctx, game, player, "enemy fire")
            end
        end
    end

    for _, threat in ipairs(ctx:active_actors_with_tags("threat")) do
        for _, player in ipairs(players) do
            if threat:is_active() and aabb_overlap(threat, player) then
                spawn_explosion(ctx, threat.position, 0.40)
                threat:despawn("rammed player")
                damage_player(ctx, game, player, "collision")
            end
        end
    end
end

local function update_enemies(ctx, dt)
    for _, enemy in ipairs(ctx:active_actors_with_tags("enemy")) do
        local p = enemy.position
        local v = enemy.velocity
        local phase = enemy:get_float("phase", 0.0) + dt * 3.1
        local base_y = enemy:get_float("base_y", p.y)
        enemy:set_float("phase", phase)
        enemy.position = Vec3(p.x + v.x * dt, clamp(base_y + math.sin(phase) * enemy:get_float("amplitude", 0.2), FIELD_BOTTOM + 0.42, FIELD_TOP - 0.42), p.z)
        if enemy.position.x < FIELD_LEFT - 1.2 then
            enemy:despawn("passed player")
        end
    end
end

local function enemy_fire(ctx, game)
    local enemies = ctx:active_actors_with_tags("enemy")
    if #enemies <= 0 then
        return
    end

    local index = 1 + math.floor(ctx:random() * #enemies)
    if index > #enemies then
        index = #enemies
    end
    local enemy = enemies[index]
    local p = enemy.position
    ctx:spawn("pool.enemy_shots", {
        position = Vec3(p.x - 0.55, p.y, 0.32),
        properties = {
            velocity = Vec3(-(5.2 + game:get_int("wave", 1) * 0.25), random_range(ctx, -0.45, 0.45), 0.0)
        }
    })
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

local function update_player_timers(ctx, dt)
    for _, player in ipairs(ctx:active_actors_with_tags("player")) do
        local timer = player:get_float("fire_timer", 0.0)
        if timer > 0.0 then
            player:set_float("fire_timer", math.max(0.0, timer - dt))
        end
        local p = player.position
        player.position = Vec3(clamp(p.x, FIELD_LEFT + 0.6, -2.0), clamp(p.y, FIELD_BOTTOM + 0.42, FIELD_TOP - 0.42), p.z)
    end
end

function armalite.update(_, _, ctx)
    local game = game_actor(ctx)
    if game == nil then
        return false
    end

    local dt = ctx.dt
    if game:get_bool("game_over", false) then
        update_explosions(ctx, dt)
        return true
    end

    update_player_timers(ctx, dt)
    update_movers(ctx, "projectile", dt)
    update_movers(ctx, "asteroid", dt)
    update_enemies(ctx, dt)

    local distance = game:get_float("distance", 0.0) + dt * 1.25
    game:set_float("distance", distance)
    game:set_int("wave", 1 + math.floor(distance / 18.0))

    local spawn_timer = game:get_float("enemy_spawn_timer", 0.0) - dt
    if spawn_timer <= 0.0 then
        spawn_enemy(ctx, game)
        local wave = game:get_int("wave", 1)
        spawn_timer = math.max(0.28, 0.86 - wave * 0.035)
        if ctx:random() > 0.72 then
            spawn_enemy(ctx, game)
        end
    end
    game:set_float("enemy_spawn_timer", spawn_timer)

    local asteroid_timer = game:get_float("asteroid_spawn_timer", 0.0) - dt
    if asteroid_timer <= 0.0 then
        spawn_asteroid(ctx, game)
        asteroid_timer = random_range(ctx, 1.0, 2.2)
    end
    game:set_float("asteroid_spawn_timer", asteroid_timer)

    local enemy_fire_timer = game:get_float("enemy_fire_timer", 0.0) - dt
    if enemy_fire_timer <= 0.0 then
        enemy_fire(ctx, game)
        enemy_fire_timer = math.max(0.42, 1.25 - game:get_int("wave", 1) * 0.04)
    end
    game:set_float("enemy_fire_timer", enemy_fire_timer)

    update_player_shot_collisions(ctx, game)
    update_player_damage(ctx, game)
    update_explosions(ctx, dt)

    local threats = #ctx:active_actors_with_tags("threat")
    game:set_int("threats_active", threats)
    if distance >= 140.0 then
        set_game_over(game, true)
    end

    return true
end

return armalite


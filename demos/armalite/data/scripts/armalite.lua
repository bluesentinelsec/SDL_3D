local armalite = {}

local FIELD_LEFT = -8.65
local FIELD_RIGHT = 8.85
local FIELD_TOP = 4.35
local FIELD_BOTTOM = -4.30
local PLAYER_X = -6.55
local PLAYER_Z = 0.18

local function game_actor(ctx)
    return ctx:actor("entity.game")
end

local function despawn_runtime_actors(ctx)
    ctx:despawn_by_tag("enemy", "new sortie")
    ctx:despawn_by_tag("asteroid", "new sortie")
    ctx:despawn_by_tag("projectile", "new sortie")
    ctx:despawn_by_tag("explosion", "new sortie")
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
    game:set_int("threats_active", 0)
    game:set_bool("game_over", false)
    game:set_bool("won", false)
    game:set_bool("paused", false)
    return true
end

local function update_movers(ctx, tag, dt)
    local _ = dt
    for _, actor in ipairs(ctx:active_actors_with_tags(tag)) do
        local position = actor.position
        if position.x < FIELD_LEFT - 1.1 or position.x > FIELD_RIGHT + 1.4 or position.y < FIELD_BOTTOM - 1.0 or position.y > FIELD_TOP + 1.0 then
            actor:despawn("out of bounds")
        end
    end
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

    local distance = game:get_float("distance", 0.0) + dt * 1.25
    game:set_float("distance", distance)
    game:set_int("wave", 1 + math.floor(distance / 18.0))

    update_explosions(ctx, dt)
    update_movers(ctx, "projectile", dt)
    update_movers(ctx, "threat", dt)

    local threats = #ctx:active_actors_with_tags("threat")
    game:set_int("threats_active", threats)
    if game:get_int("lives", 0) <= 0 then
        set_game_over(game, false)
        return true
    end
    if distance >= 140.0 then
        set_game_over(game, true)
    end

    return true
end

return armalite

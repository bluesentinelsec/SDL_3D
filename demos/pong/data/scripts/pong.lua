local pong = {}

local options_path = "user://settings/options.json"
local scores_path = "user://scores/pong_scores.json"

local function random_signed_angle(ctx, min_angle, max_angle)
    if max_angle <= 0.0 then
        return 0.0
    end

    min_angle = math.max(math.min(min_angle, max_angle), 0.0)
    local angle_span = math.max(max_angle - min_angle, 0.0)
    local angle_sign = ctx:random() < 0.5 and -1.0 or 1.0
    return angle_sign * (min_angle + ctx:random() * angle_span)
end

function pong.serve_random(ball, _, ctx)
    local base_speed = ball:get_float("base_speed", 5.6)
    local max_angle = ball:get_float("max_serve_angle", 0.52)
    local min_angle = math.min(ball:get_float("min_serve_angle", 0.14), max_angle)
    local direction = ctx:random() < 0.5 and -1.0 or 1.0
    local angle = random_signed_angle(ctx, min_angle, max_angle)
    local position = ball.position or Vec3(0.0, 0.0, 0.12)

    ball.position = Vec3(0.0, 0.0, position.z)
    ball.velocity = Vec3(math.cos(angle) * direction * base_speed, math.sin(angle) * base_speed, 0.0)
    return true
end

function pong.reflect_from_paddle(ball, payload, ctx)
    local paddle = ctx:actor(payload.other_actor_name)
    if paddle == nil then
        return false
    end

    local ball_position = ball.position
    local paddle_position = paddle.position
    if ball_position == nil or paddle_position == nil then
        return false
    end

    local paddle_half_width = paddle:get_float("half_width", 0.18)
    local paddle_half_height = paddle:get_float("half_height", 0.95)
    local ball_radius = ball:get_float("radius", 0.22)
    local base_speed = ball:get_float("base_speed", 5.6)
    local max_speed = ball:get_float("max_speed", 10.0)
    local speedup = ball:get_float("speedup_per_hit", 0.28)
    local max_angle = ball:get_float("max_reflect_angle", 1.05)
    local stagnant_epsilon = ball:get_float("stagnant_reflect_epsilon", 0.06)
    local stagnant_limit = ball:get_int("stagnant_reflect_limit", 2)
    local jitter_angle = math.min(ball:get_float("reflect_jitter_angle", 0.24), max_angle)
    local random_jitter_angle = math.min(ball:get_float("reflect_random_angle", 0.0), max_angle)
    local min_random_jitter_angle = math.min(ball:get_float("reflect_random_min_angle", 0.0), random_jitter_angle)
    local velocity = ball.velocity

    local speed = math.clamp(Vec3.length(velocity) + speedup, base_speed, max_speed)
    local impact = math.clamp((ball_position.y - paddle_position.y) / paddle_half_height, -1.0, 1.0)
    local direction = paddle_position.x < 0.0 and 1.0 or -1.0
    local angle = impact * max_angle
    local stagnant_count = ball:get_int("stagnant_reflect_count", 0)

    angle = math.clamp(angle + random_signed_angle(ctx, min_random_jitter_angle, random_jitter_angle), -max_angle, max_angle)

    local last_reflect_y = ball:get_float("last_reflect_y", ball_position.y)
    local repeated_y = math.abs(ball_position.y - last_reflect_y) <= stagnant_epsilon

    if ball:get_bool("has_last_reflect_y", false) and repeated_y then
        stagnant_count = stagnant_count + 1
    else
        stagnant_count = 1
    end

    if stagnant_limit > 0 and stagnant_count >= stagnant_limit and jitter_angle > 0.0 then
        local jitter_sign = ctx:random() < 0.5 and -1.0 or 1.0
        if math.abs(angle) < jitter_angle then
            angle = jitter_sign * jitter_angle
        else
            angle = math.clamp(angle + jitter_sign * jitter_angle, -max_angle, max_angle)
        end
        stagnant_count = 0
    end

    ball:set_bool("has_last_reflect_y", true)
    ball:set_float("last_reflect_y", ball_position.y)
    ball:set_int("stagnant_reflect_count", stagnant_count)

    ball.velocity = Vec3(math.cos(angle) * direction * speed, math.sin(angle) * speed, 0.0)
    ball.position = Vec3(paddle_position.x + direction * (paddle_half_width + ball_radius + 0.01), ball_position.y, ball_position.z)
    return true
end

function pong.cpu_track_ball(paddle, payload, ctx)
    local ball = payload.target_actor_name and ctx:actor(payload.target_actor_name) or ctx:actor_with_tags("ball")
    local ball_position = ball ~= nil and ball.position or nil
    local paddle_position = paddle.position
    if ball_position == nil or paddle_position == nil then
        return false
    end

    local speed = paddle:get_float("speed", 5.5)
    local half_height = paddle:get_float("half_height", 0.95)
    local match = ctx:actor_with_tags("state", "match")
    local field_half_height = match ~= nil and match:get_float("field_half_height", 5.0) or 5.0
    local max_step = speed * ctx.dt
    local step = math.clamp(ball_position.y - paddle_position.y, -max_step, max_step)
    local next_y = math.clamp(paddle_position.y + step, -field_half_height + half_height, field_half_height - half_height)
    paddle.position = Vec3(paddle_position.x, next_y, paddle_position.z)
    return true
end

local function settings_actor(ctx)
    return ctx:actor("entity.settings")
end

local function scores_actor(ctx)
    return ctx:actor("entity.high_scores")
end

local function read_json(ctx, path)
    local text = ctx.storage.read(path)
    if text == nil or text == "" then
        return nil
    end
    return sdl3d.json.decode(text)
end

local function write_json(ctx, path, value)
    local text = sdl3d.json.encode(value)
    if text == nil then
        return false
    end
    return ctx.storage.write(path, text)
end

local function apply_options(settings, options)
    if settings == nil or type(options) ~= "table" then
        return
    end
    if type(options.display_mode) == "string" then
        settings:set_string("display_mode", options.display_mode)
    elseif type(options.fullscreen) == "boolean" then
        settings:set_string("display_mode", options.fullscreen and "fullscreen_borderless" or "windowed")
    end
    if type(options.vsync) == "boolean" then
        settings:set_bool("vsync", options.vsync)
    end
    if type(options.renderer) == "string" then
        settings:set_string("renderer", options.renderer)
    end
    if type(options.input_style) == "string" then
        settings:set_string("input_style", options.input_style)
    end
    if type(options.keyboard_preset) == "string" then
        settings:set_string("keyboard_preset", options.keyboard_preset)
    end
    if type(options.keyboard_move) == "string" then
        settings:set_string("keyboard_move", options.keyboard_move)
    end
    if type(options.keyboard_confirm) == "string" then
        settings:set_string("keyboard_confirm", options.keyboard_confirm)
    end
    if type(options.keyboard_cancel) == "string" then
        settings:set_string("keyboard_cancel", options.keyboard_cancel)
    end
    if type(options.gamepad_icons) == "string" then
        settings:set_string("gamepad_icons", options.gamepad_icons)
    end
    if type(options.vibration) == "boolean" then
        settings:set_bool("vibration", options.vibration)
    end
    if type(options.sfx_volume) == "number" then
        settings:set_int("sfx_volume", math.floor(math.clamp(options.sfx_volume, 0, 10) + 0.5))
    end
    if type(options.music_volume) == "number" then
        settings:set_int("music_volume", math.floor(math.clamp(options.music_volume, 0, 10) + 0.5))
    end
end

local function current_options(settings)
    return {
        schema = "sdl3d.pong.options.v1",
        display_mode = settings:get_string("display_mode", "fullscreen_borderless"),
        vsync = settings:get_bool("vsync", true),
        renderer = settings:get_string("renderer", "software"),
        input_style = settings:get_string("input_style", "keyboard"),
        keyboard_preset = settings:get_string("keyboard_preset", "xbox_parity"),
        keyboard_move = settings:get_string("keyboard_move", "wasd"),
        keyboard_confirm = settings:get_string("keyboard_confirm", "enter_space"),
        keyboard_cancel = settings:get_string("keyboard_cancel", "escape_backspace"),
        gamepad_icons = settings:get_string("gamepad_icons", "xbox"),
        vibration = settings:get_bool("vibration", true),
        sfx_volume = settings:get_int("sfx_volume", 8),
        music_volume = settings:get_int("music_volume", 7),
    }
end

local function apply_scores(scores, data)
    if scores == nil or type(data) ~= "table" then
        return
    end
    scores:set_int("player_wins", tonumber(data.player_wins) or 0)
    scores:set_int("cpu_wins", tonumber(data.cpu_wins) or 0)
    scores:set_int("matches_played", tonumber(data.matches_played) or 0)
    if type(data.latest_winner) == "string" then
        scores:set_string("latest_winner", data.latest_winner)
    end
end

local function current_scores(scores)
    return {
        schema = "sdl3d.pong.scores.v1",
        player_wins = scores:get_int("player_wins", 0),
        cpu_wins = scores:get_int("cpu_wins", 0),
        matches_played = scores:get_int("matches_played", 0),
        latest_winner = scores:get_string("latest_winner", "none"),
    }
end

local function save_scores(ctx, scores)
    if scores == nil then
        return false
    end
    return write_json(ctx, scores_path, current_scores(scores))
end

local function options_persistence_enabled(ctx)
    local settings = settings_actor(ctx)
    return settings ~= nil and settings:get_bool("options_persistence_enabled", false)
end

local function score_persistence_enabled(ctx)
    local settings = settings_actor(ctx)
    return settings ~= nil and settings:get_bool("score_persistence_enabled", false)
end

function pong.load_persistence(_, _, ctx)
    local settings = settings_actor(ctx)
    if options_persistence_enabled(ctx) then
        apply_options(settings, read_json(ctx, options_path))
    end
    if score_persistence_enabled(ctx) then
        apply_scores(scores_actor(ctx), read_json(ctx, scores_path))
    end
    return true
end

function pong.save_options(_, _, ctx)
    if not options_persistence_enabled(ctx) then
        return true
    end
    local settings = settings_actor(ctx)
    if settings == nil then
        return false
    end
    return write_json(ctx, options_path, current_options(settings))
end

function pong.record_player_win(_, _, ctx)
    if not score_persistence_enabled(ctx) then
        return true
    end
    local scores = scores_actor(ctx)
    if scores == nil then
        return false
    end
    scores:set_int("player_wins", scores:get_int("player_wins", 0) + 1)
    scores:set_int("matches_played", scores:get_int("matches_played", 0) + 1)
    scores:set_string("latest_winner", "player")
    return save_scores(ctx, scores)
end

function pong.record_cpu_win(_, _, ctx)
    if not score_persistence_enabled(ctx) then
        return true
    end
    local scores = scores_actor(ctx)
    if scores == nil then
        return false
    end
    scores:set_int("cpu_wins", scores:get_int("cpu_wins", 0) + 1)
    scores:set_int("matches_played", scores:get_int("matches_played", 0) + 1)
    scores:set_string("latest_winner", "cpu")
    return save_scores(ctx, scores)
end

return pong

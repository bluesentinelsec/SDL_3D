local pong = {}

local HALF_HEIGHT = 5.0
local MAX_SERVE_ANGLE = 0.52
local MAX_REFLECT_ANGLE = 1.05

local function clamp(value, lo, hi)
    if value < lo then
        return lo
    end
    if value > hi then
        return hi
    end
    return value
end

function pong.serve_random(target)
    local base_speed = sdl3d.get_float(target, "base_speed", 5.6)
    local direction = sdl3d.random() < 0.5 and -1.0 or 1.0
    local angle = (sdl3d.random() * 2.0 - 1.0) * MAX_SERVE_ANGLE

    sdl3d.set_position(target, 0.0, 0.0, 0.12)
    sdl3d.set_vec3(target, "velocity", math.cos(angle) * direction * base_speed, math.sin(angle) * base_speed, 0.0)
    return true
end

function pong.reflect_from_paddle(target, payload)
    local paddle = payload.other_actor_name
    if paddle == nil or paddle == "" then
        return false
    end

    local ball_x, ball_y = sdl3d.get_position(target)
    local paddle_x, paddle_y = sdl3d.get_position(paddle)
    if ball_x == nil or paddle_x == nil then
        return false
    end

    local paddle_half_width = sdl3d.get_float(paddle, "half_width", 0.18)
    local paddle_half_height = sdl3d.get_float(paddle, "half_height", 0.95)
    local ball_radius = sdl3d.get_float(target, "radius", 0.22)
    local base_speed = sdl3d.get_float(target, "base_speed", 5.6)
    local max_speed = sdl3d.get_float(target, "max_speed", 10.0)
    local speedup = sdl3d.get_float(target, "speedup_per_hit", 0.28)
    local velocity_x, velocity_y = sdl3d.get_vec3(target, "velocity")

    local speed = clamp(math.sqrt(velocity_x * velocity_x + velocity_y * velocity_y) + speedup, base_speed, max_speed)
    local impact = clamp((ball_y - paddle_y) / paddle_half_height, -1.0, 1.0)
    local direction = paddle_x < 0.0 and 1.0 or -1.0
    local angle = impact * MAX_REFLECT_ANGLE

    sdl3d.set_vec3(target, "velocity", math.cos(angle) * direction * speed, math.sin(angle) * speed, 0.0)
    sdl3d.set_position(target, paddle_x + direction * (paddle_half_width + ball_radius + 0.01), ball_y, 0.12)
    return true
end

function pong.cpu_track_ball(target, payload)
    local ball = payload.target_actor_name or "entity.ball"
    local _, ball_y = sdl3d.get_position(ball)
    local paddle_x, paddle_y, paddle_z = sdl3d.get_position(target)
    if ball_y == nil or paddle_y == nil then
        return false
    end

    local speed = sdl3d.get_float(target, "speed", 5.5)
    local half_height = sdl3d.get_float(target, "half_height", 0.95)
    local max_step = speed * sdl3d.dt()
    local step = clamp(ball_y - paddle_y, -max_step, max_step)
    local next_y = clamp(paddle_y + step, -HALF_HEIGHT + half_height, HALF_HEIGHT - half_height)
    sdl3d.set_position(target, paddle_x, next_y, paddle_z or 0.0)
    return true
end

function pong.ball_chase_camera()
    return true
end

return pong

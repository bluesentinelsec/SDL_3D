local pong = {}

local HALF_HEIGHT = 5.0
local MAX_SERVE_ANGLE = 0.52
local MAX_REFLECT_ANGLE = 1.05

function pong.serve_random(ball, _, ctx)
    local base_speed = ball:get_float("base_speed", 5.6)
    local direction = ctx:random() < 0.5 and -1.0 or 1.0
    local angle = (ctx:random() * 2.0 - 1.0) * MAX_SERVE_ANGLE

    ball.position = Vec3(0.0, 0.0, 0.12)
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
    local velocity = ball.velocity

    local speed = math.clamp(Vec3.length(velocity) + speedup, base_speed, max_speed)
    local impact = math.clamp((ball_position.y - paddle_position.y) / paddle_half_height, -1.0, 1.0)
    local direction = paddle_position.x < 0.0 and 1.0 or -1.0
    local angle = impact * MAX_REFLECT_ANGLE

    ball.velocity = Vec3(math.cos(angle) * direction * speed, math.sin(angle) * speed, 0.0)
    ball.position = Vec3(paddle_position.x + direction * (paddle_half_width + ball_radius + 0.01), ball_position.y, 0.12)
    return true
end

function pong.cpu_track_ball(paddle, payload, ctx)
    local ball = ctx:actor(payload.target_actor_name or "entity.ball")
    local ball_position = ball ~= nil and ball.position or nil
    local paddle_position = paddle.position
    if ball_position == nil or paddle_position == nil then
        return false
    end

    local speed = paddle:get_float("speed", 5.5)
    local half_height = paddle:get_float("half_height", 0.95)
    local max_step = speed * ctx.dt
    local step = math.clamp(ball_position.y - paddle_position.y, -max_step, max_step)
    local next_y = math.clamp(paddle_position.y + step, -HALF_HEIGHT + half_height, HALF_HEIGHT - half_height)
    paddle.position = Vec3(paddle_position.x, next_y, paddle_position.z)
    return true
end

function pong.ball_chase_camera()
    return true
end

return pong

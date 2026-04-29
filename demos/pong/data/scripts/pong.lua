local pong = {}

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

return pong

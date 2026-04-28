local rules = {}

function rules.run(target, _, ctx)
    target.position = Vec3(1.0, 2.0, 3.0)
    target.velocity = Vec3(test.shared.speed(), 2.0, 0.0)
    target:set_float("speed_length", Vec3.length(target.velocity))
    ctx:actor("entity.target"):set_bool("ctx_ok", ctx.adapter == "adapter.test.run" and ctx.dt >= 0.0)
    ctx:state_set("last_adapter", ctx.adapter)
    target:set_bool("state_ok", ctx:state_get("last_adapter", "") == "adapter.test.run")
    target:set_float("random_value", ctx:random())
    return true
end

return rules

local rules = {}

function rules.run(target)
    sdl3d.set_vec3(target, "velocity", test.shared.speed(), 2.0, 0.0)
    return true
end

return rules

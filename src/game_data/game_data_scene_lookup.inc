/* Runtime scene, entity, asset, and controller lookup helpers. Included by src/game_data.c to preserve internal
 * linkage. */

static yyjson_val *runtime_root(const slayer3d_game_data_runtime *runtime)
{
    return runtime != NULL && runtime->doc != NULL ? yyjson_doc_get_root(runtime->doc) : NULL;
}

static scene_entry *active_scene_entry(slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->active_scene_index < 0 || runtime->active_scene_index >= runtime->scene_count)
        return NULL;
    return &runtime->scenes[runtime->active_scene_index];
}

static const scene_entry *active_scene_entry_const(const slayer3d_game_data_runtime *runtime)
{
    if (runtime == NULL || runtime->active_scene_index < 0 || runtime->active_scene_index >= runtime->scene_count)
        return NULL;
    return &runtime->scenes[runtime->active_scene_index];
}

static scene_entry *find_scene(slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->scene_count; ++i)
    {
        if (runtime->scenes[i].name != NULL && SDL_strcmp(runtime->scenes[i].name, name) == 0)
            return &runtime->scenes[i];
    }
    return NULL;
}

static const scene_entry *find_scene_const(const slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->scene_count; ++i)
    {
        if (runtime->scenes[i].name != NULL && SDL_strcmp(runtime->scenes[i].name, name) == 0)
            return &runtime->scenes[i];
    }
    return NULL;
}

static sector_door_runtime *find_sector_door(slayer3d_game_data_runtime *runtime, const char *name)
{
    if (runtime == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < runtime->sector_door_count; ++i)
    {
        sector_door_runtime *door = &runtime->sector_doors[i];
        if (door->door.name != NULL && SDL_strcmp(door->door.name, name) == 0)
            return door;
    }
    return NULL;
}

static bool sector_door_in_scene(const sector_door_runtime *door, const char *scene_name)
{
    if (door == NULL)
        return false;
    const char *scene = json_string(door->json, "scene", NULL);
    if (scene == NULL || scene[0] == '\0')
        return true;
    return scene_name != NULL && SDL_strcmp(scene, scene_name) == 0;
}

static bool sector_door_in_active_scene(const slayer3d_game_data_runtime *runtime, const sector_door_runtime *door)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    return sector_door_in_scene(door, scene != NULL ? scene->name : NULL);
}

static bool sector_platform_in_scene(const sector_platform_runtime *platform, const char *scene_name)
{
    if (platform == NULL)
        return false;
    const char *scene = json_string(platform->json, "scene", NULL);
    if (scene == NULL || scene[0] == '\0')
        return true;
    return scene_name != NULL && SDL_strcmp(scene, scene_name) == 0;
}

static bool sector_platform_in_active_scene(const slayer3d_game_data_runtime *runtime,
                                            const sector_platform_runtime *platform)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    return sector_platform_in_scene(platform, scene != NULL ? scene->name : NULL);
}

static scene_menu_state *find_scene_menu(scene_entry *scene, const char *name)
{
    if (scene == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < scene->menu_count; ++i)
    {
        const char *menu_name = json_string(scene->menus[i].menu, "name", NULL);
        if (menu_name != NULL && SDL_strcmp(menu_name, name) == 0)
            return &scene->menus[i];
    }
    return NULL;
}

static const scene_menu_state *find_scene_menu_const(const scene_entry *scene, const char *name)
{
    if (scene == NULL || name == NULL)
        return NULL;
    for (int i = 0; i < scene->menu_count; ++i)
    {
        const char *menu_name = json_string(scene->menus[i].menu, "name", NULL);
        if (menu_name != NULL && SDL_strcmp(menu_name, name) == 0)
            return &scene->menus[i];
    }
    return NULL;
}

static bool scene_has_entity(const scene_entry *scene, const char *entity_name)
{
    if (scene == NULL || !scene->has_entity_filter)
        return true;
    if (entity_name == NULL)
        return false;
    for (int i = 0; i < scene->entity_count; ++i)
    {
        if (scene->entities[i] != NULL && SDL_strcmp(scene->entities[i], entity_name) == 0)
            return true;
    }
    return false;
}

static bool active_scene_has_entity_internal(const slayer3d_game_data_runtime *runtime, const char *entity_name)
{
    const scene_entry *scene = active_scene_entry_const(runtime);
    if (scene_has_entity(scene, entity_name))
        return true;
    if (runtime == NULL || scene == NULL || entity_name == NULL)
        return false;
    return actor_pool_in_scene(find_actor_pool_for_actor_const(runtime, entity_name, NULL), scene->name);
}

static void apply_scene_camera(slayer3d_game_data_runtime *runtime, const scene_entry *scene)
{
    const char *camera = scene != NULL ? json_string(scene->root, "camera", NULL) : NULL;
    if (runtime != NULL && camera != NULL)
        runtime->active_camera = camera;
}

static slayer3d_properties *create_scene_enter_payload(const char *from_scene, const char *to_scene,
                                                       const slayer3d_properties *payload)
{
    slayer3d_properties *enter_payload = slayer3d_properties_create();
    if (enter_payload == NULL)
        return NULL;

    const int count = slayer3d_properties_count(payload);
    for (int i = 0; i < count; ++i)
    {
        const char *key = NULL;
        if (slayer3d_properties_get_key_at(payload, i, &key, NULL))
            copy_property_value(enter_payload, key, slayer3d_properties_get_value(payload, key));
    }

    slayer3d_properties_set_string(enter_payload, "from_scene", from_scene != NULL ? from_scene : "");
    slayer3d_properties_set_string(enter_payload, "to_scene", to_scene != NULL ? to_scene : "");
    return enter_payload;
}

static void emit_scene_enter_signal(slayer3d_game_data_runtime *runtime, const scene_entry *scene,
                                    const char *from_scene, const slayer3d_properties *payload)
{
    if (runtime == NULL || scene == NULL)
        return;

    const int signal_id = slayer3d_game_data_find_signal(runtime, json_string(scene->root, "on_enter_signal", NULL));
    if (signal_id >= 0 && runtime_bus(runtime) != NULL)
    {
        slayer3d_properties *enter_payload = create_scene_enter_payload(from_scene, scene->name, payload);
        slayer3d_signal_emit(runtime_bus(runtime), signal_id, enter_payload);
        slayer3d_properties_destroy(enter_payload);
    }
}

static yyjson_val *find_entity_json(const slayer3d_game_data_runtime *runtime, const char *name)
{
    yyjson_val *entities = obj_get(runtime_root(runtime), "entities");
    for (size_t i = 0; yyjson_is_arr(entities) && i < yyjson_arr_size(entities); ++i)
    {
        yyjson_val *entity = yyjson_arr_get(entities, i);
        const char *entity_name = json_string(entity, "name", NULL);
        if (entity_name != NULL && name != NULL && SDL_strcmp(entity_name, name) == 0)
            return entity;
    }
    return NULL;
}

static bool entity_json_has_tag(yyjson_val *entity, const char *tag)
{
    yyjson_val *tags = obj_get(entity, "tags");
    for (size_t i = 0; tag != NULL && yyjson_is_arr(tags) && i < yyjson_arr_size(tags); ++i)
    {
        yyjson_val *item = yyjson_arr_get(tags, i);
        if (yyjson_is_str(item) && SDL_strcmp(yyjson_get_str(item), tag) == 0)
            return true;
    }
    return false;
}

static bool entity_json_has_tags(yyjson_val *entity, const char *const *tags, int tag_count)
{
    if (tag_count <= 0)
        return false;
    for (int i = 0; i < tag_count; ++i)
    {
        if (!entity_json_has_tag(entity, tags[i]))
            return false;
    }
    return true;
}

static bool entity_json_has_all_tags_from_json(yyjson_val *entity, yyjson_val *tags)
{
    if (!yyjson_is_arr(tags) || yyjson_arr_size(tags) == 0)
        return false;

    for (size_t i = 0; i < yyjson_arr_size(tags); ++i)
    {
        yyjson_val *tag = yyjson_arr_get(tags, i);
        if (!yyjson_is_str(tag) || !entity_json_has_tag(entity, yyjson_get_str(tag)))
            return false;
    }
    return true;
}

static yyjson_val *find_component_json(yyjson_val *entity, const char *type)
{
    yyjson_val *components = obj_get(entity, "components");
    for (size_t i = 0; yyjson_is_arr(components) && i < yyjson_arr_size(components); ++i)
    {
        yyjson_val *component = yyjson_arr_get(components, i);
        const char *component_type = json_string(component, "type", NULL);
        if (component_type != NULL && type != NULL && SDL_strcmp(component_type, type) == 0)
            return component;
    }
    return NULL;
}

static yyjson_val *find_actor_definition_json(const slayer3d_game_data_runtime *runtime, const char *actor_name)
{
    yyjson_val *entity = find_entity_json(runtime, actor_name);
    if (entity != NULL)
        return entity;

    const actor_pool_runtime *pool = find_actor_pool_for_actor_const(runtime, actor_name, NULL);
    return pool != NULL ? pool->archetype_json : NULL;
}

static yyjson_val *find_font_json(const slayer3d_game_data_runtime *runtime, const char *id)
{
    yyjson_val *fonts = obj_get(obj_get(runtime_root(runtime), "assets"), "fonts");
    for (size_t i = 0; id != NULL && yyjson_is_arr(fonts) && i < yyjson_arr_size(fonts); ++i)
    {
        yyjson_val *font = yyjson_arr_get(fonts, i);
        const char *font_id = json_string(font, "id", NULL);
        if (font_id != NULL && SDL_strcmp(font_id, id) == 0)
            return font;
    }
    return NULL;
}

static yyjson_val *find_image_json(const slayer3d_game_data_runtime *runtime, const char *id)
{
    yyjson_val *images = obj_get(obj_get(runtime_root(runtime), "assets"), "images");
    for (size_t i = 0; id != NULL && yyjson_is_arr(images) && i < yyjson_arr_size(images); ++i)
    {
        yyjson_val *image = yyjson_arr_get(images, i);
        const char *image_id = json_string(image, "id", NULL);
        if (image_id != NULL && SDL_strcmp(image_id, id) == 0)
            return image;
    }
    return NULL;
}

static yyjson_val *find_model_json(const slayer3d_game_data_runtime *runtime, const char *id)
{
    yyjson_val *models = obj_get(obj_get(runtime_root(runtime), "assets"), "models");
    for (size_t i = 0; id != NULL && yyjson_is_arr(models) && i < yyjson_arr_size(models); ++i)
    {
        yyjson_val *model = yyjson_arr_get(models, i);
        const char *model_id = json_string(model, "id", NULL);
        if (model_id != NULL && SDL_strcmp(model_id, id) == 0)
            return model;
    }
    return NULL;
}

static yyjson_val *find_sound_json(const slayer3d_game_data_runtime *runtime, const char *id)
{
    yyjson_val *sounds = obj_get(obj_get(runtime_root(runtime), "assets"), "sounds");
    for (size_t i = 0; id != NULL && yyjson_is_arr(sounds) && i < yyjson_arr_size(sounds); ++i)
    {
        yyjson_val *sound = yyjson_arr_get(sounds, i);
        const char *sound_id = json_string(sound, "id", NULL);
        if (sound_id != NULL && SDL_strcmp(sound_id, id) == 0)
            return sound;
    }
    return NULL;
}

static yyjson_val *find_music_json(const slayer3d_game_data_runtime *runtime, const char *id)
{
    yyjson_val *music_assets = obj_get(obj_get(runtime_root(runtime), "assets"), "music");
    for (size_t i = 0; id != NULL && yyjson_is_arr(music_assets) && i < yyjson_arr_size(music_assets); ++i)
    {
        yyjson_val *music = yyjson_arr_get(music_assets, i);
        const char *music_id = json_string(music, "id", NULL);
        if (music_id != NULL && SDL_strcmp(music_id, id) == 0)
            return music;
    }
    return NULL;
}

static yyjson_val *find_ambient_json(const slayer3d_game_data_runtime *runtime, const char *id)
{
    yyjson_val *ambient_assets = obj_get(obj_get(runtime_root(runtime), "assets"), "ambient");
    for (size_t i = 0; id != NULL && yyjson_is_arr(ambient_assets) && i < yyjson_arr_size(ambient_assets); ++i)
    {
        yyjson_val *ambient = yyjson_arr_get(ambient_assets, i);
        const char *ambient_id = json_string(ambient, "id", NULL);
        if (ambient_id != NULL && SDL_strcmp(ambient_id, id) == 0)
            return ambient;
    }
    return NULL;
}

static yyjson_val *find_sprite_json(const slayer3d_game_data_runtime *runtime, const char *id)
{
    yyjson_val *sprites = obj_get(obj_get(runtime_root(runtime), "assets"), "sprites");
    for (size_t i = 0; id != NULL && yyjson_is_arr(sprites) && i < yyjson_arr_size(sprites); ++i)
    {
        yyjson_val *sprite = yyjson_arr_get(sprites, i);
        const char *sprite_id = json_string(sprite, "id", NULL);
        if (sprite_id != NULL && SDL_strcmp(sprite_id, id) == 0)
            return sprite;
    }
    return NULL;
}

static slayer3d_audio_bus parse_audio_bus(const char *bus, slayer3d_audio_bus fallback)
{
    if (bus == NULL)
        return fallback;
    if (SDL_strcmp(bus, "music") == 0)
        return SLAYER3D_AUDIO_BUS_MUSIC;
    if (SDL_strcmp(bus, "sound_effects") == 0 || SDL_strcmp(bus, "sfx") == 0)
        return SLAYER3D_AUDIO_BUS_SOUND_EFFECTS;
    if (SDL_strcmp(bus, "dialogue") == 0 || SDL_strcmp(bus, "dialog") == 0)
        return SLAYER3D_AUDIO_BUS_DIALOGUE;
    if (SDL_strcmp(bus, "ambience") == 0 || SDL_strcmp(bus, "ambiance") == 0 || SDL_strcmp(bus, "ambient") == 0)
        return SLAYER3D_AUDIO_BUS_AMBIENCE;
    return fallback;
}

static yyjson_val *find_camera_json(const slayer3d_game_data_runtime *runtime, const char *name)
{
    yyjson_val *cameras = obj_get(obj_get(runtime_root(runtime), "world"), "cameras");
    for (size_t i = 0; yyjson_is_arr(cameras) && i < yyjson_arr_size(cameras); ++i)
    {
        yyjson_val *camera = yyjson_arr_get(cameras, i);
        const char *camera_name = json_string(camera, "name", NULL);
        if (camera_name != NULL && name != NULL && SDL_strcmp(camera_name, name) == 0)
            return camera;
    }
    return NULL;
}

static fps_controller_runtime *find_fps_controller(slayer3d_game_data_runtime *runtime, const char *entity_name)
{
    if (runtime == NULL || entity_name == NULL)
        return NULL;
    for (int i = 0; i < runtime->fps_controller_count; ++i)
    {
        fps_controller_runtime *controller = &runtime->fps_controllers[i];
        if (controller->entity_name != NULL && SDL_strcmp(controller->entity_name, entity_name) == 0)
            return controller;
    }
    return NULL;
}

static const fps_controller_runtime *find_fps_controller_const(const slayer3d_game_data_runtime *runtime,
                                                               const char *entity_name)
{
    if (runtime == NULL || entity_name == NULL)
        return NULL;
    for (int i = 0; i < runtime->fps_controller_count; ++i)
    {
        const fps_controller_runtime *controller = &runtime->fps_controllers[i];
        if (controller->entity_name != NULL && SDL_strcmp(controller->entity_name, entity_name) == 0)
            return controller;
    }
    return NULL;
}

static fps_controller_runtime *find_or_add_fps_controller(slayer3d_game_data_runtime *runtime, const char *entity_name,
                                                          yyjson_val *component)
{
    fps_controller_runtime *controller = find_fps_controller(runtime, entity_name);
    if (controller != NULL)
    {
        if (component != NULL && controller->component != component)
        {
            controller->component = component;
            controller->initialized = false;
        }
        return controller;
    }
    if (runtime == NULL || entity_name == NULL || component == NULL)
        return NULL;
    if (runtime->fps_controller_count >= runtime->fps_controller_capacity)
    {
        const int next_capacity = runtime->fps_controller_capacity > 0 ? runtime->fps_controller_capacity * 2 : 4;
        fps_controller_runtime *controllers = (fps_controller_runtime *)SDL_realloc(
            runtime->fps_controllers, (size_t)next_capacity * sizeof(*controllers));
        if (controllers == NULL)
            return NULL;
        runtime->fps_controllers = controllers;
        runtime->fps_controller_capacity = next_capacity;
    }

    controller = &runtime->fps_controllers[runtime->fps_controller_count++];
    SDL_zero(*controller);
    controller->entity_name = entity_name;
    controller->component = component;
    return controller;
}

static patrol_controller_runtime *find_patrol_controller(slayer3d_game_data_runtime *runtime, const char *entity_name)
{
    if (runtime == NULL || entity_name == NULL)
        return NULL;
    for (int i = 0; i < runtime->patrol_controller_count; ++i)
    {
        patrol_controller_runtime *controller = &runtime->patrol_controllers[i];
        if (controller->entity_name != NULL && SDL_strcmp(controller->entity_name, entity_name) == 0)
            return controller;
    }
    return NULL;
}

static patrol_controller_runtime *find_or_add_patrol_controller(slayer3d_game_data_runtime *runtime,
                                                                const char *entity_name, yyjson_val *component)
{
    patrol_controller_runtime *controller = find_patrol_controller(runtime, entity_name);
    if (controller != NULL)
    {
        if (component != NULL && controller->component != component)
        {
            controller->component = component;
            controller->initialized = false;
        }
        return controller;
    }
    if (runtime == NULL || entity_name == NULL || component == NULL)
        return NULL;
    if (runtime->patrol_controller_count >= runtime->patrol_controller_capacity)
    {
        const int next_capacity = runtime->patrol_controller_capacity > 0 ? runtime->patrol_controller_capacity * 2 : 4;
        patrol_controller_runtime *controllers = (patrol_controller_runtime *)SDL_realloc(
            runtime->patrol_controllers, (size_t)next_capacity * sizeof(*controllers));
        if (controllers == NULL)
            return NULL;
        runtime->patrol_controllers = controllers;
        runtime->patrol_controller_capacity = next_capacity;
    }

    controller = &runtime->patrol_controllers[runtime->patrol_controller_count++];
    SDL_zero(*controller);
    controller->entity_name = entity_name;
    controller->component = component;
    return controller;
}

static slayer3d_backend parse_backend(const char *value, slayer3d_backend fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "auto") == 0)
        return SLAYER3D_BACKEND_AUTO;
    if (SDL_strcasecmp(value, "software") == 0)
        return SLAYER3D_BACKEND_SOFTWARE;
    if (SDL_strcasecmp(value, "opengl") == 0 || SDL_strcasecmp(value, "gl") == 0 || SDL_strcasecmp(value, "gpu") == 0)
        return SLAYER3D_BACKEND_OPENGL;
    return fallback;
}

static slayer3d_window_mode parse_window_mode(const char *value, slayer3d_window_mode fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "windowed") == 0 || SDL_strcasecmp(value, "window") == 0)
        return SLAYER3D_WINDOW_MODE_WINDOWED;
    if (SDL_strcasecmp(value, "fullscreen_exclusive") == 0 || SDL_strcasecmp(value, "exclusive") == 0)
        return SLAYER3D_WINDOW_MODE_FULLSCREEN_EXCLUSIVE;
    if (SDL_strcasecmp(value, "fullscreen_borderless") == 0 || SDL_strcasecmp(value, "borderless") == 0 ||
        SDL_strcasecmp(value, "desktop_fullscreen") == 0)
        return SLAYER3D_WINDOW_MODE_FULLSCREEN_BORDERLESS;
    return fallback;
}

static void storage_config_from_root(yyjson_val *root, slayer3d_storage_config *out_config);

static slayer3d_tonemap_mode parse_tonemap(const char *value, slayer3d_tonemap_mode fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "none") == 0)
        return SLAYER3D_TONEMAP_NONE;
    if (SDL_strcasecmp(value, "reinhard") == 0)
        return SLAYER3D_TONEMAP_REINHARD;
    if (SDL_strcasecmp(value, "aces") == 0)
        return SLAYER3D_TONEMAP_ACES;
    return fallback;
}

static bool parse_render_profile(const char *value, slayer3d_render_profile *out_profile)
{
    if (value == NULL || out_profile == NULL)
        return false;
    if (SDL_strcasecmp(value, "modern") == 0)
        *out_profile = slayer3d_profile_modern();
    else if (SDL_strcasecmp(value, "ps1") == 0)
        *out_profile = slayer3d_profile_ps1();
    else if (SDL_strcasecmp(value, "n64") == 0)
        *out_profile = slayer3d_profile_n64();
    else if (SDL_strcasecmp(value, "dos") == 0)
        *out_profile = slayer3d_profile_dos();
    else if (SDL_strcasecmp(value, "snes") == 0)
        *out_profile = slayer3d_profile_snes();
    else if (SDL_strcasecmp(value, "grayscale") == 0)
        *out_profile = slayer3d_profile_grayscale();
    else if (SDL_strcasecmp(value, "gameboy") == 0)
        *out_profile = slayer3d_profile_gameboy();
    else
        return false;
    return true;
}

static const char *scene_state_string(const slayer3d_game_data_runtime *runtime, const char *key, const char *fallback)
{
    if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0')
        return fallback;
    return slayer3d_properties_get_string(runtime->scene_state, key, fallback);
}

static bool scene_state_bool(const slayer3d_game_data_runtime *runtime, const char *key, bool fallback)
{
    if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0')
        return fallback;
    const slayer3d_value *value = slayer3d_properties_get_value(runtime->scene_state, key);
    if (value == NULL)
        return fallback;
    if (value->type == SLAYER3D_VALUE_BOOL)
        return value->as_bool;
    if (value->type == SLAYER3D_VALUE_INT)
        return value->as_int != 0;
    if (value->type == SLAYER3D_VALUE_STRING && value->as_string != NULL)
    {
        return SDL_strcasecmp(value->as_string, "true") == 0 || SDL_strcmp(value->as_string, "1") == 0 ||
               SDL_strcasecmp(value->as_string, "on") == 0 || SDL_strcasecmp(value->as_string, "yes") == 0;
    }
    return fallback;
}

static float scene_state_float(const slayer3d_game_data_runtime *runtime, const char *key, float fallback)
{
    if (runtime == NULL || runtime->scene_state == NULL || key == NULL || key[0] == '\0')
        return fallback;
    const slayer3d_value *value = slayer3d_properties_get_value(runtime->scene_state, key);
    if (value == NULL)
        return fallback;
    if (value->type == SLAYER3D_VALUE_FLOAT)
        return value->as_float;
    if (value->type == SLAYER3D_VALUE_INT)
        return (float)value->as_int;
    return fallback;
}

static bool camera_fov_degrees_valid(float value)
{
    return value > 0.0f && value < 180.0f;
}

static slayer3d_camera_fov_axis parse_camera_fov_axis(const char *value, slayer3d_camera_fov_axis fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "vertical") == 0)
        return SLAYER3D_CAMERA_FOV_VERTICAL;
    if (SDL_strcasecmp(value, "horizontal") == 0)
        return SLAYER3D_CAMERA_FOV_HORIZONTAL;
    return fallback;
}

static float camera_fov_degrees(const slayer3d_game_data_runtime *runtime, yyjson_val *camera_json, float fallback)
{
    const float authored = json_float(camera_json, "fov", json_float(camera_json, "fovy", fallback));
    const float valid_authored = camera_fov_degrees_valid(authored) ? authored : fallback;
    const float runtime_value = scene_state_float(runtime, json_string(camera_json, "fov_key", NULL), valid_authored);
    return camera_fov_degrees_valid(runtime_value) ? runtime_value : valid_authored;
}

static slayer3d_camera_fov_axis camera_fov_axis(const slayer3d_game_data_runtime *runtime, yyjson_val *camera_json)
{
    const slayer3d_camera_fov_axis authored =
        parse_camera_fov_axis(json_string(camera_json, "fov_axis", NULL), SLAYER3D_CAMERA_FOV_VERTICAL);
    return parse_camera_fov_axis(scene_state_string(runtime, json_string(camera_json, "fov_axis_key", NULL),
                                                    json_string(camera_json, "fov_axis", NULL)),
                                 authored);
}

static slayer3d_transition_type parse_transition_type(const char *value, slayer3d_transition_type fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "fade") == 0)
        return SLAYER3D_TRANSITION_FADE;
    if (SDL_strcasecmp(value, "circle") == 0)
        return SLAYER3D_TRANSITION_CIRCLE;
    if (SDL_strcasecmp(value, "melt") == 0)
        return SLAYER3D_TRANSITION_MELT;
    if (SDL_strcasecmp(value, "pixelate") == 0)
        return SLAYER3D_TRANSITION_PIXELATE;
    return fallback;
}

static slayer3d_transition_direction parse_transition_direction(const char *value,
                                                                slayer3d_transition_direction fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "in") == 0)
        return SLAYER3D_TRANSITION_IN;
    if (SDL_strcasecmp(value, "out") == 0)
        return SLAYER3D_TRANSITION_OUT;
    return fallback;
}

static slayer3d_builtin_font parse_builtin_font(const char *value, slayer3d_builtin_font fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "Inter") == 0 || SDL_strcasecmp(value, "inter") == 0)
        return SLAYER3D_BUILTIN_FONT_INTER;
    return fallback;
}

static slayer3d_game_data_ui_align parse_ui_align(const char *value, slayer3d_game_data_ui_align fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "left") == 0)
        return SLAYER3D_GAME_DATA_UI_ALIGN_LEFT;
    if (SDL_strcasecmp(value, "center") == 0 || SDL_strcasecmp(value, "middle") == 0)
        return SLAYER3D_GAME_DATA_UI_ALIGN_CENTER;
    if (SDL_strcasecmp(value, "right") == 0)
        return SLAYER3D_GAME_DATA_UI_ALIGN_RIGHT;
    return fallback;
}

static slayer3d_game_data_ui_valign parse_ui_valign(const char *value, slayer3d_game_data_ui_valign fallback)
{
    if (value == NULL)
        return fallback;
    if (SDL_strcasecmp(value, "top") == 0)
        return SLAYER3D_GAME_DATA_UI_VALIGN_TOP;
    if (SDL_strcasecmp(value, "center") == 0 || SDL_strcasecmp(value, "middle") == 0)
        return SLAYER3D_GAME_DATA_UI_VALIGN_CENTER;
    if (SDL_strcasecmp(value, "bottom") == 0)
        return SLAYER3D_GAME_DATA_UI_VALIGN_BOTTOM;
    return fallback;
}

static const char *parse_ui_image_effect(const char *value)
{
    if (value == NULL || value[0] == '\0')
        return NULL;
    if (SDL_strcasecmp(value, "melt") == 0)
        return "melt";
    return value;
}

static int axis_index(const char *axis)
{
    if (axis == NULL)
        return -1;
    if (SDL_strcmp(axis, "x") == 0)
        return 0;
    if (SDL_strcmp(axis, "y") == 0)
        return 1;
    if (SDL_strcmp(axis, "z") == 0)
        return 2;
    return -1;
}

static float vec_axis(slayer3d_vec3 value, int axis)
{
    if (axis == 0)
        return value.x;
    if (axis == 1)
        return value.y;
    if (axis == 2)
        return value.z;
    return 0.0f;
}

static void set_vec_axis(slayer3d_vec3 *value, int axis, float component)
{
    if (value == NULL)
        return;
    if (axis == 0)
        value->x = component;
    else if (axis == 1)
        value->y = component;
    else if (axis == 2)
        value->z = component;
}

static slayer3d_vec3 actor_vec_property(const slayer3d_registered_actor *actor, const char *key)
{
    return actor != NULL ? slayer3d_properties_get_vec3(actor->props, key, slayer3d_vec3_make(0.0f, 0.0f, 0.0f))
                         : slayer3d_vec3_make(0.0f, 0.0f, 0.0f);
}

static void actor_set_position(slayer3d_registered_actor *actor, slayer3d_vec3 position)
{
    if (actor == NULL)
        return;
    actor->position = position;
    slayer3d_properties_set_vec3(actor->props, "origin", position);
}

typedef struct game_data_snapshot_value
{
    slayer3d_replication_field_type type;
    union {
        bool as_bool;
        Sint32 as_int32;
        float as_float32;
        slayer3d_vec2 as_vec2;
        slayer3d_vec3 as_vec3;
    } value;
} game_data_snapshot_value;

typedef struct game_data_input_value
{
    int action_id;
    float value;
} game_data_input_value;

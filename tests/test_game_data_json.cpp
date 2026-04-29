#include <gtest/gtest.h>

#include <set>
#include <string>

extern "C"
{
#include "yyjson/yyjson.h"
}

namespace
{

const char *kPongDataPath = SDL3D_PONG_DATA_PATH;

class JsonDoc
{
  public:
    explicit JsonDoc(const char *path)
    {
        yyjson_read_err err{};
        doc_ = yyjson_read_file(path, YYJSON_READ_NOFLAG, nullptr, &err);
        if (doc_ == nullptr)
        {
            error_ = std::string("yyjson error ") + std::to_string(err.code) + " at byte " + std::to_string(err.pos) +
                     ": " + (err.msg != nullptr ? err.msg : "");
        }
    }

    ~JsonDoc()
    {
        yyjson_doc_free(doc_);
    }

    yyjson_val *root() const
    {
        return yyjson_doc_get_root(doc_);
    }

    const std::string &error() const
    {
        return error_;
    }

  private:
    yyjson_doc *doc_ = nullptr;
    std::string error_;
};

std::string required_string(yyjson_val *object, const char *key)
{
    yyjson_val *value = yyjson_obj_get(object, key);
    EXPECT_TRUE(yyjson_is_str(value)) << "Expected string key: " << key;
    const char *text = yyjson_get_str(value);
    return text != nullptr ? std::string(text) : std::string();
}

yyjson_val *required_object(yyjson_val *object, const char *key)
{
    yyjson_val *value = yyjson_obj_get(object, key);
    EXPECT_TRUE(yyjson_is_obj(value)) << "Expected object key: " << key;
    return value;
}

yyjson_val *required_array(yyjson_val *object, const char *key)
{
    yyjson_val *value = yyjson_obj_get(object, key);
    EXPECT_TRUE(yyjson_is_arr(value)) << "Expected array key: " << key;
    return value;
}

void collect_named_objects(yyjson_val *array, const char *field, std::set<std::string> *names)
{
    ASSERT_TRUE(yyjson_is_arr(array));
    const size_t count = yyjson_arr_size(array);
    for (size_t i = 0; i < count; ++i)
    {
        yyjson_val *item = yyjson_arr_get(array, i);
        ASSERT_TRUE(yyjson_is_obj(item));
        const std::string name = required_string(item, field);
        EXPECT_FALSE(name.empty());
        EXPECT_TRUE(names->insert(name).second) << "Duplicate authored name: " << name;
    }
}

void collect_signals(yyjson_val *signals, std::set<std::string> *names)
{
    ASSERT_TRUE(yyjson_is_arr(signals));
    const size_t count = yyjson_arr_size(signals);
    for (size_t i = 0; i < count; ++i)
    {
        yyjson_val *item = yyjson_arr_get(signals, i);
        ASSERT_TRUE(yyjson_is_str(item));
        const char *name = yyjson_get_str(item);
        ASSERT_NE(name, nullptr);
        EXPECT_TRUE(names->insert(name).second) << "Duplicate signal: " << name;
    }
}

void expect_ref(const std::set<std::string> &names, yyjson_val *object, const char *key)
{
    yyjson_val *value = yyjson_obj_get(object, key);
    if (value == nullptr)
    {
        return;
    }

    ASSERT_TRUE(yyjson_is_str(value)) << "Expected named reference at key: " << key;
    const char *name = yyjson_get_str(value);
    ASSERT_NE(name, nullptr);
    EXPECT_NE(names.find(name), names.end()) << "Missing referenced name: " << name;
}

void validate_action_refs(yyjson_val *action, const std::set<std::string> &entities,
                          const std::set<std::string> &signals, const std::set<std::string> &timers,
                          const std::set<std::string> &cameras, const std::set<std::string> &adapters);

void validate_action_array(yyjson_val *actions, const std::set<std::string> &entities,
                           const std::set<std::string> &signals, const std::set<std::string> &timers,
                           const std::set<std::string> &cameras, const std::set<std::string> &adapters)
{
    ASSERT_TRUE(yyjson_is_arr(actions));
    const size_t count = yyjson_arr_size(actions);
    for (size_t i = 0; i < count; ++i)
    {
        yyjson_val *action = yyjson_arr_get(actions, i);
        ASSERT_TRUE(yyjson_is_obj(action));
        validate_action_refs(action, entities, signals, timers, cameras, adapters);
    }
}

void validate_branch_refs(yyjson_val *action, const std::set<std::string> &entities,
                          const std::set<std::string> &signals, const std::set<std::string> &timers,
                          const std::set<std::string> &cameras, const std::set<std::string> &adapters)
{
    yyjson_val *condition = yyjson_obj_get(action, "if");
    if (condition != nullptr)
    {
        ASSERT_TRUE(yyjson_is_obj(condition));
        expect_ref(entities, condition, "target");
    }

    yyjson_val *then_actions = yyjson_obj_get(action, "then");
    if (then_actions != nullptr)
    {
        validate_action_array(then_actions, entities, signals, timers, cameras, adapters);
    }

    yyjson_val *else_actions = yyjson_obj_get(action, "else");
    if (else_actions != nullptr)
    {
        validate_action_array(else_actions, entities, signals, timers, cameras, adapters);
    }
}

void validate_action_refs(yyjson_val *action, const std::set<std::string> &entities,
                          const std::set<std::string> &signals, const std::set<std::string> &timers,
                          const std::set<std::string> &cameras, const std::set<std::string> &adapters)
{
    const std::string type = required_string(action, "type");
    EXPECT_FALSE(type.empty());

    expect_ref(entities, action, "target");
    expect_ref(signals, action, "signal");
    expect_ref(timers, action, "timer");
    expect_ref(cameras, action, "camera");
    expect_ref(cameras, action, "fallback");
    expect_ref(adapters, action, "adapter");

    if (type == "branch")
    {
        validate_branch_refs(action, entities, signals, timers, cameras, adapters);
    }
}

} // namespace

TEST(GameDataJson, PongDataParsesAsStrictJson)
{
    JsonDoc doc(kPongDataPath);
    ASSERT_NE(doc.root(), nullptr) << doc.error();
    EXPECT_TRUE(yyjson_is_obj(doc.root()));
}

TEST(GameDataJson, PongDataDeclaresGenericTopLevelModel)
{
    JsonDoc doc(kPongDataPath);
    ASSERT_NE(doc.root(), nullptr) << doc.error();

    EXPECT_EQ(required_string(doc.root(), "schema"), "sdl3d.game.v0");
    EXPECT_TRUE(yyjson_is_obj(yyjson_obj_get(doc.root(), "metadata")));
    EXPECT_TRUE(yyjson_is_obj(yyjson_obj_get(doc.root(), "world")));
    EXPECT_TRUE(yyjson_is_arr(yyjson_obj_get(doc.root(), "entities")));
    EXPECT_TRUE(yyjson_is_arr(yyjson_obj_get(doc.root(), "scripts")));
    EXPECT_TRUE(yyjson_is_obj(yyjson_obj_get(doc.root(), "logic")));
}

TEST(GameDataJson, PongUsesStandardOptionsScenePackage)
{
    JsonDoc doc(kPongDataPath);
    ASSERT_NE(doc.root(), nullptr) << doc.error();

    yyjson_val *scenes = required_object(doc.root(), "scenes");
    yyjson_val *files = required_array(scenes, "files");
    bool saw_package = false;
    bool saw_legacy_options_file = false;
    for (size_t i = 0; i < yyjson_arr_size(files); ++i)
    {
        yyjson_val *entry = yyjson_arr_get(files, i);
        if (yyjson_is_obj(entry))
        {
            EXPECT_EQ(required_string(entry, "package"), "standard_options");
            saw_package = true;
        }
        if (yyjson_is_str(entry))
        {
            const std::string path = yyjson_get_str(entry) != nullptr ? yyjson_get_str(entry) : "";
            if (path.find("options") != std::string::npos)
            {
                saw_legacy_options_file = true;
            }
        }
    }
    EXPECT_TRUE(saw_package);
    EXPECT_FALSE(saw_legacy_options_file);

    yyjson_val *options = required_object(scenes, "standard_options");
    EXPECT_EQ(required_string(options, "settings"), "entity.settings");
    EXPECT_EQ(required_string(options, "return_scene"), "scene.title");
    EXPECT_TRUE(yyjson_is_arr(yyjson_obj_get(required_object(options, "bindings"), "keyboard")));
    EXPECT_TRUE(yyjson_is_arr(yyjson_obj_get(required_object(options, "bindings"), "gamepad")));
}

TEST(GameDataJson, PongDataUsesNonSectorFixedScreenWorld)
{
    JsonDoc doc(kPongDataPath);
    ASSERT_NE(doc.root(), nullptr) << doc.error();

    yyjson_val *world = required_object(doc.root(), "world");
    EXPECT_EQ(required_string(world, "kind"), "fixed_screen");
    EXPECT_EQ(yyjson_obj_get(world, "sectors"), nullptr);
    EXPECT_EQ(yyjson_obj_get(world, "portals"), nullptr);
}

TEST(GameDataJson, PongDataHasUniqueAuthoredNames)
{
    JsonDoc doc(kPongDataPath);
    ASSERT_NE(doc.root(), nullptr) << doc.error();

    std::set<std::string> entity_names;
    std::set<std::string> signal_names;
    std::set<std::string> adapter_names;
    std::set<std::string> script_names;
    collect_named_objects(required_array(doc.root(), "entities"), "name", &entity_names);
    collect_signals(required_array(doc.root(), "signals"), &signal_names);
    collect_named_objects(required_array(doc.root(), "adapters"), "name", &adapter_names);
    collect_named_objects(required_array(doc.root(), "scripts"), "id", &script_names);

    EXPECT_NE(entity_names.find("entity.ball"), entity_names.end());
    EXPECT_NE(entity_names.find("entity.score.player"), entity_names.end());
    EXPECT_NE(signal_names.find("signal.ball.serve"), signal_names.end());
    EXPECT_NE(adapter_names.find("adapter.pong.reflect_from_paddle"), adapter_names.end());
    EXPECT_NE(script_names.find("script.pong"), script_names.end());
}

TEST(GameDataJson, PongLogicReferencesKnownEntitiesSignalsTimersCamerasAndAdapters)
{
    JsonDoc doc(kPongDataPath);
    ASSERT_NE(doc.root(), nullptr) << doc.error();

    std::set<std::string> entities;
    std::set<std::string> signals;
    std::set<std::string> timers;
    std::set<std::string> cameras;
    std::set<std::string> adapters;
    std::set<std::string> scripts;

    collect_named_objects(required_array(doc.root(), "entities"), "name", &entities);
    collect_signals(required_array(doc.root(), "signals"), &signals);
    collect_named_objects(required_array(doc.root(), "adapters"), "name", &adapters);
    collect_named_objects(required_array(doc.root(), "scripts"), "id", &scripts);

    yyjson_val *adapter_array = required_array(doc.root(), "adapters");
    yyjson_val *script_array = required_array(doc.root(), "scripts");
    for (size_t i = 0; i < yyjson_arr_size(script_array); ++i)
    {
        yyjson_val *script = yyjson_arr_get(script_array, i);
        ASSERT_TRUE(yyjson_is_obj(script));
        EXPECT_FALSE(required_string(script, "module").empty());
        EXPECT_FALSE(required_string(script, "path").empty());
    }

    for (size_t i = 0; i < yyjson_arr_size(adapter_array); ++i)
    {
        yyjson_val *adapter = yyjson_arr_get(adapter_array, i);
        ASSERT_TRUE(yyjson_is_obj(adapter));
        expect_ref(scripts, adapter, "script");
        EXPECT_FALSE(required_string(adapter, "function").empty());
    }

    yyjson_val *world = required_object(doc.root(), "world");
    collect_named_objects(required_array(world, "cameras"), "name", &cameras);

    yyjson_val *logic = required_object(doc.root(), "logic");
    collect_named_objects(required_array(logic, "timers"), "name", &timers);

    std::set<std::string> actions;
    yyjson_val *input_contexts = required_array(required_object(doc.root(), "input"), "contexts");
    for (size_t i = 0; i < yyjson_arr_size(input_contexts); ++i)
    {
        yyjson_val *context = yyjson_arr_get(input_contexts, i);
        ASSERT_TRUE(yyjson_is_obj(context));
        collect_named_objects(required_array(context, "actions"), "name", &actions);
    }
    yyjson_val *pause = required_object(required_object(doc.root(), "app"), "pause");
    expect_ref(actions, pause, "action");
    yyjson_val *allowed_if = required_object(pause, "allowed_if");
    expect_ref(entities, allowed_if, "target");
    EXPECT_EQ(required_string(allowed_if, "type"), "property.compare");

    yyjson_val *sensors = required_array(logic, "sensors");
    for (size_t i = 0; i < yyjson_arr_size(sensors); ++i)
    {
        yyjson_val *sensor = yyjson_arr_get(sensors, i);
        ASSERT_TRUE(yyjson_is_obj(sensor));
        expect_ref(entities, sensor, "entity");
        expect_ref(entities, sensor, "a");
        expect_ref(entities, sensor, "b");
        expect_ref(signals, sensor, "on_enter");
        expect_ref(signals, sensor, "on_pressed");
        expect_ref(signals, sensor, "on_reflect");
    }

    yyjson_val *bindings = required_array(logic, "bindings");
    for (size_t i = 0; i < yyjson_arr_size(bindings); ++i)
    {
        yyjson_val *binding = yyjson_arr_get(bindings, i);
        ASSERT_TRUE(yyjson_is_obj(binding));
        expect_ref(signals, binding, "signal");
        validate_action_array(required_array(binding, "actions"), entities, signals, timers, cameras, adapters);
    }
}

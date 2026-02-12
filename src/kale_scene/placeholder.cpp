// Kale 场景管理层 - 占位源文件

#include <kale_scene/scene_manager.hpp>
#include <kale_scene/scene_types.hpp>

namespace kale::scene {

void placeholder() {
    SceneManager mgr;
    (void)mgr.GetNode(kInvalidSceneNodeHandle);
}

}  // namespace kale::scene

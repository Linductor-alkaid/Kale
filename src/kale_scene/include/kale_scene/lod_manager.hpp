/**
 * @file lod_manager.hpp
 * @brief LOD 管理器：根据相机与节点距离选择 LOD 并写入 Renderable
 *
 * 与 scene_management_layer_design.md、phase10-10.5 对齐。
 * CullScene 内调用 SelectLOD(node, camera)；支持 LOD 的 Renderable 实现 GetLODCount/SetCurrentLOD，
 * GetMesh() 返回选定 LOD 的 mesh。
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kale::scene {

class SceneNode;
class CameraNode;

/**
 * LOD 管理器：根据相机到节点的距离选择 LOD 等级，并写入 Renderable::SetCurrentLOD。
 * 距离阈值：thresholds[i] 表示进入 LOD i+1 的最小距离；LOD 0 为最高细节。
 * 例如 thresholds = {10, 50, 200} 表示 4 档：d<10 LOD0, 10<=d<50 LOD1, 50<=d<200 LOD2, d>=200 LOD3。
 */
class LODManager {
public:
    LODManager() = default;

    /**
     * 根据 node 与 camera 的距离选择 LOD，并调用 Renderable::SetCurrentLOD。
     * 使用 node 世界矩阵平移分量与 camera 世界矩阵平移分量的距离。
     * 若 node 无 Renderable 或 GetLODCount()<=1 则仅设 SetCurrentLOD(0)。
     */
    void SelectLOD(SceneNode* node, CameraNode* camera) const;

    /**
     * 设置距离阈值（升序）。LOD 档数 = thresholds.size() + 1。
     * 默认 {20, 80, 300} 即 4 档。
     */
    void SetDistanceThresholds(std::vector<float> thresholds) { distanceThresholds_ = std::move(thresholds); }
    /** 当前距离阈值 */
    const std::vector<float>& GetDistanceThresholds() const { return distanceThresholds_; }

private:
    std::vector<float> distanceThresholds_{20.f, 80.f, 300.f};
};

}  // namespace kale::scene
